#include "scriptvm/ir/typed_ir.h"

#include "core/hash/sha256.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <utility>

namespace arp::scriptvm {

namespace {

using Hash = std::uint64_t;

constexpr Hash kFnvOffset = 14695981039346656037ull;
constexpr Hash kFnvPrime = 1099511628211ull;

void hashBytes(Hash& hash, const char* data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= kFnvPrime;
    }
}

void hashString(Hash& hash, const core::pmr::String& value)
{
    hashBytes(hash, value.data(), value.size());
    hashBytes(hash, "\0", 1);
}

void hashInt(Hash& hash, std::int64_t value)
{
    const auto unsignedValue = static_cast<std::uint64_t>(value);
    for (int i = 0; i < 8; ++i) {
        const char byte = static_cast<char>((unsignedValue >> (i * 8)) & 0xffu);
        hashBytes(hash, &byte, 1);
    }
}

core::pmr::String hashToString(Hash hash)
{
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return core::pmr::String(buffer, &core::defaultCpuMemoryResource());
}

core::pmr::String sha256ToString(const core::pmr::String& input)
{
    const auto digest = core::Sha256::digest(core::StringView(input.data(), input.size()));
    static constexpr char kHex[] = "0123456789abcdef";
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    result = "sha256:";
    result.reserve(7u + digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(kHex[(byte >> 4u) & 0x0fu]);
        result.push_back(kHex[byte & 0x0fu]);
    }
    return result;
}

void appendCanonicalType(core::pmr::String& out, const TypedIrTypeRef& type)
{
    out += std::to_string(static_cast<int>(type.kind));
    out += ":";
    out += type.nullable ? "1:" : "0:";
    out += type.className;
    out += "|";
}

core::pmr::String canonicalSignature(const TypedIrSignature& signature)
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    result += "params=";
    result += std::to_string(signature.parameters.size());
    result += ";";
    for (const auto& parameter : signature.parameters)
        appendCanonicalType(result, parameter);
    result += "result=";
    appendCanonicalType(result, signature.result);
    result += "variadic=";
    result += signature.variadic ? "1;" : "0;";
    result += "fixed=";
    result += std::to_string(signature.fixedParameterCount);
    result += ";element=";
    appendCanonicalType(result, signature.variadicElement);
    return result;
}

void hashType(Hash& hash, const TypedIrTypeRef& type)
{
    hashInt(hash, static_cast<std::int64_t>(type.kind));
    hashInt(hash, type.nullable ? 1 : 0);
    hashString(hash, type.className);
}

void hashSignature(Hash& hash, const TypedIrSignature& signature)
{
    hashInt(hash, static_cast<std::int64_t>(signature.parameters.size()));
    for (const auto& parameter : signature.parameters)
        hashType(hash, parameter);
    hashType(hash, signature.result);
    hashInt(hash, signature.variadic ? 1 : 0);
    hashInt(hash, signature.fixedParameterCount);
    hashType(hash, signature.variadicElement);
}

void hashStringVector(Hash& hash, const core::pmr::Vector<core::pmr::String>& values)
{
    core::pmr::Vector<core::pmr::String> sorted{&core::defaultCpuMemoryResource()};
    sorted.insert(sorted.end(), values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    hashInt(hash, static_cast<std::int64_t>(sorted.size()));
    for (const auto& value : sorted)
        hashString(hash, value);
}

TypedIrOperation classify(TypedIrOpcode opcode)
{
    switch (opcode) {
    case TypedIrOpcode::LoadNil:
    case TypedIrOpcode::LoadBool:
    case TypedIrOpcode::LoadInt:
    case TypedIrOpcode::LoadInt64:
    case TypedIrOpcode::LoadFloat:
    case TypedIrOpcode::LoadString:
    case TypedIrOpcode::LoadLocal:
    case TypedIrOpcode::LoadCapture:
    case TypedIrOpcode::LoadGlobal:
        return TypedIrOperation::Load;
    case TypedIrOpcode::StoreLocal:
    case TypedIrOpcode::StoreCapture:
    case TypedIrOpcode::StoreGlobal:
    case TypedIrOpcode::Pop:
        return TypedIrOperation::Store;
    case TypedIrOpcode::Add:
    case TypedIrOpcode::Sub:
    case TypedIrOpcode::Mul:
    case TypedIrOpcode::Div:
    case TypedIrOpcode::Mod:
    case TypedIrOpcode::Pow:
    case TypedIrOpcode::FloorDiv:
    case TypedIrOpcode::Neg:
    case TypedIrOpcode::Not:
        return TypedIrOperation::Arithmetic;
    case TypedIrOpcode::BitAnd:
    case TypedIrOpcode::BitOr:
    case TypedIrOpcode::BitXor:
    case TypedIrOpcode::BitClear:
    case TypedIrOpcode::ShiftLeft:
    case TypedIrOpcode::ShiftRight:
        return TypedIrOperation::Bitwise;
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
        return TypedIrOperation::Compare;
    case TypedIrOpcode::ConvertInt:
    case TypedIrOpcode::ConvertInt64:
    case TypedIrOpcode::ConvertFloat:
    case TypedIrOpcode::ConvertString:
    case TypedIrOpcode::ConvertBool:
    case TypedIrOpcode::ConvertBytes:
        return TypedIrOperation::Convert;
    case TypedIrOpcode::Jump:
    case TypedIrOpcode::JumpIfFalse:
    case TypedIrOpcode::JumpIfTrue:
    case TypedIrOpcode::JumpIfLocalIntLtConstFalse:
        return TypedIrOperation::ControlFlow;
    case TypedIrOpcode::Call:
    case TypedIrOpcode::CallValue:
    case TypedIrOpcode::MakeFuncValue:
    case TypedIrOpcode::MakeClosure:
    case TypedIrOpcode::MakeMethodValue:
        return TypedIrOperation::Call;
    case TypedIrOpcode::CallHost:
        return TypedIrOperation::Host;
    case TypedIrOpcode::MakeList:
    case TypedIrOpcode::MakeDict:
    case TypedIrOpcode::MakeInstance:
    case TypedIrOpcode::MakeRange:
    case TypedIrOpcode::MakeRangeFromStack:
        return TypedIrOperation::Container;
    case TypedIrOpcode::LoadIndex:
    case TypedIrOpcode::StoreIndex:
    case TypedIrOpcode::LoadField:
    case TypedIrOpcode::StoreField:
    case TypedIrOpcode::AddrIndex:
    case TypedIrOpcode::AddrField:
    case TypedIrOpcode::AddrLocal:
    case TypedIrOpcode::AddrCapture:
    case TypedIrOpcode::AddrGlobal:
    case TypedIrOpcode::Deref:
    case TypedIrOpcode::StoreIndirect:
        return TypedIrOperation::Access;
    case TypedIrOpcode::BuiltinLen:
    case TypedIrOpcode::BuiltinAppend:
    case TypedIrOpcode::BuiltinDelete:
    case TypedIrOpcode::BuiltinClear:
    case TypedIrOpcode::RangeInit:
    case TypedIrOpcode::RangeNext:
    case TypedIrOpcode::LoadModule:
    case TypedIrOpcode::LoadModuleExport:
        return TypedIrOperation::Other;
    case TypedIrOpcode::Yield:
    case TypedIrOpcode::Await:
        return TypedIrOperation::Suspend;
    case TypedIrOpcode::PushHandler:
    case TypedIrOpcode::PopHandler:
    case TypedIrOpcode::Throw:
        return TypedIrOperation::Exception;
    case TypedIrOpcode::Return:
    case TypedIrOpcode::ReturnInt:
        return TypedIrOperation::Return;
    case TypedIrOpcode::LocalAddIntConst:
    case TypedIrOpcode::LocalSubIntConst:
    case TypedIrOpcode::LocalAddInt64Const:
    case TypedIrOpcode::LocalSubInt64Const:
    case TypedIrOpcode::LocalAddFloatConst:
    case TypedIrOpcode::LocalSubFloatConst:
    case TypedIrOpcode::LocalAddLocal:
    case TypedIrOpcode::IntAdd:
    case TypedIrOpcode::IntSub:
    case TypedIrOpcode::IntMul:
    case TypedIrOpcode::IntDiv:
    case TypedIrOpcode::IntMod:
        return TypedIrOperation::Arithmetic;
    case TypedIrOpcode::OpcodeCount:
        return TypedIrOperation::Other;
    }
    return TypedIrOperation::Other;
}

bool isTerminator(TypedIrOpcode opcode)
{
    return opcode == TypedIrOpcode::Jump || opcode == TypedIrOpcode::Return
        || opcode == TypedIrOpcode::ReturnInt || opcode == TypedIrOpcode::Throw
        || opcode == TypedIrOpcode::Yield || opcode == TypedIrOpcode::Await;
}

bool isConditionalBranch(TypedIrOpcode opcode)
{
    return opcode == TypedIrOpcode::JumpIfFalse || opcode == TypedIrOpcode::JumpIfTrue
        || opcode == TypedIrOpcode::JumpIfLocalIntLtConstFalse;
}

std::uint32_t operationEffects(TypedIrOpcode opcode)
{
    std::uint32_t effects = static_cast<std::uint32_t>(TypedIrEffect::None);
    if (opcode == TypedIrOpcode::LoadGlobal)
        effects |= static_cast<std::uint32_t>(TypedIrEffect::ReadsGlobal);
    if (opcode == TypedIrOpcode::StoreGlobal)
        effects |= static_cast<std::uint32_t>(TypedIrEffect::WritesGlobal);
    if (opcode == TypedIrOpcode::CallHost)
        effects |= static_cast<std::uint32_t>(TypedIrEffect::ReadsHost)
            | static_cast<std::uint32_t>(TypedIrEffect::WritesHost)
            | static_cast<std::uint32_t>(TypedIrEffect::MayRaise);
    if (opcode == TypedIrOpcode::Yield || opcode == TypedIrOpcode::Await)
        effects |= static_cast<std::uint32_t>(TypedIrEffect::MaySuspend);
    if (opcode == TypedIrOpcode::Throw || opcode == TypedIrOpcode::Call
        || opcode == TypedIrOpcode::CallValue || opcode == TypedIrOpcode::MakeInstance
        || opcode == TypedIrOpcode::LoadIndex || opcode == TypedIrOpcode::StoreIndex
        || opcode == TypedIrOpcode::LoadField || opcode == TypedIrOpcode::StoreField
        || opcode == TypedIrOpcode::Deref || opcode == TypedIrOpcode::StoreIndirect
        || opcode == TypedIrOpcode::Div || opcode == TypedIrOpcode::Mod
        || opcode == TypedIrOpcode::Pow || opcode == TypedIrOpcode::FloorDiv
        || opcode == TypedIrOpcode::LocalAddIntConst || opcode == TypedIrOpcode::LocalSubIntConst
        || opcode == TypedIrOpcode::LocalAddInt64Const
        || opcode == TypedIrOpcode::LocalSubInt64Const
        || opcode == TypedIrOpcode::LocalAddFloatConst
        || opcode == TypedIrOpcode::LocalSubFloatConst || opcode == TypedIrOpcode::LocalAddLocal)
        effects |= static_cast<std::uint32_t>(TypedIrEffect::MayRaise);
    return effects;
}

TypedIrCallKind callKind(TypedIrOpcode opcode)
{
    switch (opcode) {
    case TypedIrOpcode::Call:
        return TypedIrCallKind::Direct;
    case TypedIrOpcode::CallValue:
        return TypedIrCallKind::Indirect;
    case TypedIrOpcode::MakeMethodValue:
        return TypedIrCallKind::Method;
    case TypedIrOpcode::CallHost:
        return TypedIrCallKind::Host;
    case TypedIrOpcode::MakeInstance:
        return TypedIrCallKind::Reflection;
    default:
        return TypedIrCallKind::None;
    }
}

struct ClosureProvenance {
    int functionIndex = -1;
    int captureCount = -1;
    bool scriptMethod = false;

    bool known() const { return functionIndex >= 0 && (captureCount >= 0 || scriptMethod); }

    bool operator==(const ClosureProvenance& other) const
    {
        return functionIndex == other.functionIndex && captureCount == other.captureCount
            && scriptMethod == other.scriptMethod;
    }

    bool operator!=(const ClosureProvenance& other) const { return !(*this == other); }
};

struct ClosureProvenanceState {
    core::pmr::Vector<ClosureProvenance> stack{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ClosureProvenance> locals{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ClosureProvenance> globals{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ClosureProvenance> captures{&core::defaultCpuMemoryResource()};
};

bool mergeClosureProvenanceState(
    ClosureProvenanceState& destination, const ClosureProvenanceState& source)
{
    if (destination.stack.size() != source.stack.size()
        || destination.locals.size() != source.locals.size()
        || destination.globals.size() != source.globals.size()
        || destination.captures.size() != source.captures.size())
        return false;
    bool changed = false;
    for (std::size_t index = 0; index < destination.stack.size(); ++index) {
        if (!(destination.stack[index] == source.stack[index])
            && destination.stack[index].known()) {
            destination.stack[index] = {};
            changed = true;
        }
    }
    for (std::size_t index = 0; index < destination.locals.size(); ++index) {
        if (!(destination.locals[index] == source.locals[index])
            && destination.locals[index].known()) {
            destination.locals[index] = {};
            changed = true;
        }
    }
    for (std::size_t index = 0; index < destination.globals.size(); ++index) {
        if (!(destination.globals[index] == source.globals[index])
            && destination.globals[index].known()) {
            destination.globals[index] = {};
            changed = true;
        }
    }
    for (std::size_t index = 0; index < destination.captures.size(); ++index) {
        if (!(destination.captures[index] == source.captures[index])
            && destination.captures[index].known()) {
            destination.captures[index] = {};
            changed = true;
        }
    }
    return changed;
}

int functionIndexById(const TypedIrModule& module, const core::pmr::String& functionId)
{
    const auto target = std::find_if(
        module.functions.begin(), module.functions.end(), [&functionId](const auto& function) {
            return function.id == functionId;
        });
    return target == module.functions.end() ? -1
                                            : static_cast<int>(target - module.functions.begin());
}

ScriptResult invalid(core::pmr::String message)
{
    return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid, std::move(message));
}

std::uint16_t operandFields(TypedIrOpcode opcode)
{
    using Field = TypedIrOperandField;
    switch (opcode) {
    case TypedIrOpcode::LoadBool:
    case TypedIrOpcode::LoadLocal:
    case TypedIrOpcode::LoadCapture:
    case TypedIrOpcode::LoadGlobal:
    case TypedIrOpcode::StoreLocal:
    case TypedIrOpcode::StoreCapture:
    case TypedIrOpcode::StoreGlobal:
    case TypedIrOpcode::AddrLocal:
    case TypedIrOpcode::AddrCapture:
    case TypedIrOpcode::AddrGlobal:
    case TypedIrOpcode::Return:
        return static_cast<std::uint16_t>(Field::A);
    case TypedIrOpcode::LoadInt:
    case TypedIrOpcode::LoadInt64:
    case TypedIrOpcode::LoadFloat:
    case TypedIrOpcode::MakeRange:
        return static_cast<std::uint16_t>(Field::I64);
    case TypedIrOpcode::LoadString:
    case TypedIrOpcode::LoadField:
    case TypedIrOpcode::StoreField:
    case TypedIrOpcode::AddrField:
    case TypedIrOpcode::LoadModule:
    case TypedIrOpcode::LoadModuleExport:
        return static_cast<std::uint16_t>(Field::Text);
    case TypedIrOpcode::Jump:
    case TypedIrOpcode::JumpIfFalse:
    case TypedIrOpcode::JumpIfTrue:
        return static_cast<std::uint16_t>(Field::BranchTarget);
    case TypedIrOpcode::PushHandler:
        return Field::BranchTarget | Field::Text;
    case TypedIrOpcode::Call:
    case TypedIrOpcode::MakeClosure:
        return Field::FunctionTargetId | Field::B;
    case TypedIrOpcode::MakeFuncValue:
    case TypedIrOpcode::MakeMethodValue:
        return static_cast<std::uint16_t>(Field::FunctionTargetId);
    case TypedIrOpcode::CallValue:
    case TypedIrOpcode::CallHost:
        return Field::B | Field::FunctionTargetId | Field::CallableBindingId;
    case TypedIrOpcode::MakeList:
    case TypedIrOpcode::MakeDict:
    case TypedIrOpcode::MakeRangeFromStack:
        return static_cast<std::uint16_t>(Field::B);
    case TypedIrOpcode::MakeInstance:
        return Field::B | Field::Text;
    case TypedIrOpcode::RangeInit:
    case TypedIrOpcode::LocalAddLocal:
        return Field::A | Field::B;
    case TypedIrOpcode::RangeNext:
        return Field::A | Field::B | Field::I64;
    case TypedIrOpcode::LocalAddIntConst:
    case TypedIrOpcode::LocalSubIntConst:
    case TypedIrOpcode::LocalAddFloatConst:
    case TypedIrOpcode::LocalSubFloatConst:
    case TypedIrOpcode::LocalAddInt64Const:
    case TypedIrOpcode::LocalSubInt64Const:
        return Field::A | Field::I64;
    case TypedIrOpcode::JumpIfLocalIntLtConstFalse:
        return Field::BranchTarget | Field::B | Field::I64;
    default:
        return static_cast<std::uint16_t>(Field::None);
    }
}

std::pair<std::int8_t, std::int8_t> stackEffect(TypedIrOpcode opcode)
{
    constexpr auto dynamic = kTypedIrDynamicStackEffect;
    switch (opcode) {
    case TypedIrOpcode::LoadNil:
    case TypedIrOpcode::LoadBool:
    case TypedIrOpcode::LoadInt:
    case TypedIrOpcode::LoadInt64:
    case TypedIrOpcode::LoadFloat:
    case TypedIrOpcode::LoadString:
    case TypedIrOpcode::LoadLocal:
    case TypedIrOpcode::LoadCapture:
    case TypedIrOpcode::LoadGlobal:
    case TypedIrOpcode::AddrLocal:
    case TypedIrOpcode::AddrCapture:
    case TypedIrOpcode::AddrGlobal:
    case TypedIrOpcode::LoadModule:
    case TypedIrOpcode::MakeFuncValue:
    case TypedIrOpcode::MakeRange:
        return {0, 1};
    case TypedIrOpcode::StoreLocal:
    case TypedIrOpcode::StoreCapture:
    case TypedIrOpcode::StoreGlobal:
    case TypedIrOpcode::Pop:
    case TypedIrOpcode::Throw:
    case TypedIrOpcode::Yield:
        return {1, 0};
    case TypedIrOpcode::Add:
    case TypedIrOpcode::Sub:
    case TypedIrOpcode::Mul:
    case TypedIrOpcode::Div:
    case TypedIrOpcode::Mod:
    case TypedIrOpcode::BitAnd:
    case TypedIrOpcode::BitOr:
    case TypedIrOpcode::BitXor:
    case TypedIrOpcode::BitClear:
    case TypedIrOpcode::ShiftLeft:
    case TypedIrOpcode::ShiftRight:
    case TypedIrOpcode::CmpEq:
    case TypedIrOpcode::CmpNe:
    case TypedIrOpcode::CmpLt:
    case TypedIrOpcode::CmpLe:
    case TypedIrOpcode::CmpGt:
    case TypedIrOpcode::CmpGe:
    case TypedIrOpcode::IntAdd:
    case TypedIrOpcode::IntSub:
    case TypedIrOpcode::IntMul:
    case TypedIrOpcode::IntDiv:
    case TypedIrOpcode::IntMod:
    case TypedIrOpcode::IntCmpEq:
    case TypedIrOpcode::IntCmpNe:
    case TypedIrOpcode::IntCmpLt:
    case TypedIrOpcode::IntCmpLe:
    case TypedIrOpcode::IntCmpGt:
    case TypedIrOpcode::IntCmpGe:
    case TypedIrOpcode::Pow:
    case TypedIrOpcode::FloorDiv:
        return {2, 1};
    case TypedIrOpcode::Neg:
    case TypedIrOpcode::Not:
    case TypedIrOpcode::ConvertInt:
    case TypedIrOpcode::ConvertInt64:
    case TypedIrOpcode::ConvertFloat:
    case TypedIrOpcode::ConvertString:
    case TypedIrOpcode::ConvertBool:
    case TypedIrOpcode::ConvertBytes:
    case TypedIrOpcode::Deref:
    case TypedIrOpcode::BuiltinLen:
    case TypedIrOpcode::Await:
    case TypedIrOpcode::LoadField:
    case TypedIrOpcode::LoadModuleExport:
        return {1, 1};
    case TypedIrOpcode::LoadIndex:
    case TypedIrOpcode::AddrIndex:
        return {2, 1};
    case TypedIrOpcode::StoreIndex:
        return {3, 0};
    case TypedIrOpcode::StoreField:
    case TypedIrOpcode::StoreIndirect:
        return {2, 0};
    case TypedIrOpcode::AddrField:
    case TypedIrOpcode::MakeMethodValue:
        return {1, 1};
    case TypedIrOpcode::BuiltinAppend:
        return {2, 1};
    case TypedIrOpcode::BuiltinDelete:
        return {2, 0};
    case TypedIrOpcode::BuiltinClear:
    case TypedIrOpcode::RangeInit:
        return {1, 0};
    case TypedIrOpcode::RangeNext:
        return {0, 1};
    case TypedIrOpcode::JumpIfFalse:
    case TypedIrOpcode::JumpIfTrue:
        return {1, 0};
    case TypedIrOpcode::ReturnInt:
        return {1, 0};
    case TypedIrOpcode::Call:
    case TypedIrOpcode::CallValue:
    case TypedIrOpcode::CallHost:
    case TypedIrOpcode::MakeClosure:
    case TypedIrOpcode::MakeList:
    case TypedIrOpcode::MakeDict:
    case TypedIrOpcode::MakeInstance:
    case TypedIrOpcode::MakeRangeFromStack:
    case TypedIrOpcode::Return:
        return {dynamic, dynamic};
    default:
        return {0, 0};
    }
}

} // namespace

core::pmr::String makeTypedIrFunctionId(
    const core::pmr::String& moduleId, const core::pmr::String& functionName)
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    result = "arp-fn:";
    result += moduleId;
    result += "/";
    result += functionName;
    return result;
}

core::pmr::String typedIrSignatureHash(const TypedIrSignature& signature)
{
    return sha256ToString(canonicalSignature(signature));
}

const TypedIrOperationDescriptor& typedIrOperationDescriptor(TypedIrOpcode opcode)
{
    static constexpr std::array<const char*, static_cast<std::size_t>(TypedIrOpcode::OpcodeCount)>
        names{{"LoadNil",
            "LoadBool",
            "LoadInt",
            "LoadInt64",
            "LoadFloat",
            "LoadString",
            "LoadLocal",
            "LoadCapture",
            "LoadGlobal",
            "StoreLocal",
            "StoreCapture",
            "StoreGlobal",
            "Pop",
            "Return",
            "Add",
            "Sub",
            "Mul",
            "Div",
            "Mod",
            "Neg",
            "Not",
            "BitAnd",
            "BitOr",
            "BitXor",
            "BitClear",
            "ShiftLeft",
            "ShiftRight",
            "CmpEq",
            "CmpNe",
            "CmpLt",
            "CmpLe",
            "CmpGt",
            "CmpGe",
            "Jump",
            "JumpIfFalse",
            "JumpIfTrue",
            "Call",
            "CallValue",
            "CallHost",
            "MakeFuncValue",
            "MakeClosure",
            "MakeMethodValue",
            "MakeList",
            "MakeDict",
            "MakeInstance",
            "MakeRange",
            "MakeRangeFromStack",
            "LoadIndex",
            "StoreIndex",
            "LoadField",
            "StoreField",
            "AddrIndex",
            "AddrField",
            "AddrLocal",
            "AddrCapture",
            "AddrGlobal",
            "Deref",
            "StoreIndirect",
            "BuiltinLen",
            "BuiltinAppend",
            "BuiltinDelete",
            "BuiltinClear",
            "RangeInit",
            "RangeNext",
            "ConvertInt",
            "ConvertInt64",
            "ConvertFloat",
            "ConvertString",
            "ConvertBool",
            "ConvertBytes",
            "LocalAddIntConst",
            "LocalSubIntConst",
            "LocalAddLocal",
            "LocalAddFloatConst",
            "LocalSubFloatConst",
            "JumpIfLocalIntLtConstFalse",
            "IntAdd",
            "IntSub",
            "IntMul",
            "IntDiv",
            "IntMod",
            "IntCmpEq",
            "IntCmpNe",
            "IntCmpLt",
            "IntCmpLe",
            "IntCmpGt",
            "IntCmpGe",
            "ReturnInt",
            "Yield",
            "Await",
            "PushHandler",
            "PopHandler",
            "Throw",
            "LoadModule",
            "LoadModuleExport",
            "Pow",
            "FloorDiv",
            "LocalAddInt64Const",
            "LocalSubInt64Const"}};
    static const auto descriptors = [] {
        std::array<TypedIrOperationDescriptor, static_cast<std::size_t>(TypedIrOpcode::OpcodeCount)>
            result{};
        for (std::size_t index = 0; index < result.size(); ++index) {
            const auto typedOpcode = static_cast<TypedIrOpcode>(index);
            auto& descriptor = result[index];
            descriptor.name = names[index];
            descriptor.operation = classify(typedOpcode);
            descriptor.callKind = callKind(typedOpcode);
            descriptor.requiredEffects = operationEffects(typedOpcode);
            descriptor.operandFields = operandFields(typedOpcode);
            const auto effect = stackEffect(typedOpcode);
            descriptor.stackInputs = effect.first;
            descriptor.stackOutputs = effect.second;
            descriptor.terminator = isTerminator(typedOpcode);
            descriptor.conditionalBranch = isConditionalBranch(typedOpcode);
            descriptor.suspension =
                typedOpcode == TypedIrOpcode::Yield || typedOpcode == TypedIrOpcode::Await;
        }
        return result;
    }();
    const auto index = static_cast<std::size_t>(opcode);
    static const TypedIrOperationDescriptor invalidDescriptor{};
    return index < descriptors.size() ? descriptors[index] : invalidDescriptor;
}

core::pmr::String typedIrOpcodeName(TypedIrOpcode opcode)
{
    return core::pmr::String(
        typedIrOperationDescriptor(opcode).name, &core::defaultCpuMemoryResource());
}

core::pmr::String computeTypedIrFingerprint(const TypedIrModule& module)
{
    Hash hash = kFnvOffset;
    hashString(hash, module.moduleId);
    hashInt(hash, module.moduleVersion);
    hashInt(hash, module.semanticAbiVersion);
    hashInt(hash, module.typedIrVersion);
    hashInt(hash, module.globalCount);
    for (const auto& global : module.globals) {
        hashString(hash, global.name);
        hashInt(hash, global.slot);
    }
    hashString(hash, module.moduleInitFunctionId);
    hashStringVector(hash, module.dependencies);
    hashStringVector(hash, module.reflectionDependencies);
    hashStringVector(hash, module.hostCapabilities);
    for (const auto& binding : module.callableBindings) {
        hashString(hash, binding.bindingId);
        hashInt(hash, static_cast<std::int64_t>(binding.origin));
        hashString(hash, binding.targetId);
        hashString(hash, binding.signatureHash);
        hashInt(hash, binding.fixedArity);
        hashInt(hash, binding.maySuspend ? 1 : 0);
        hashInt(hash, binding.generatorBridge ? 1 : 0);
        hashInt(hash, binding.lifetimeVersion);
    }
    for (const auto& schema : module.scriptClassSchemas) {
        hashString(hash, schema.className);
        hashStringVector(hash, schema.fields);
        for (const auto& method : schema.methods) {
            hashString(hash, method.first);
            hashString(hash, method.second);
        }
        hashString(hash, schema.constructorFunctionId);
    }

    core::pmr::Vector<std::size_t> order{&core::defaultCpuMemoryResource()};
    order.reserve(module.functions.size());
    for (std::size_t i = 0; i < module.functions.size(); ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&module](std::size_t lhs, std::size_t rhs) {
        return module.functions[lhs].id < module.functions[rhs].id;
    });

    for (const std::size_t index : order) {
        const auto& function = module.functions[index];
        hashString(hash, function.id);
        hashString(hash, function.name);
        hashSignature(hash, function.signature);
        hashInt(hash, function.parameterCount);
        hashInt(hash, function.localCount);
        hashInt(hash, function.captureCount);
        hashInt(hash, function.maxStack);
        hashInt(hash, function.isGenerator ? 1 : 0);
        hashInt(hash, function.hasYieldFrom ? 1 : 0);
        hashInt(hash, function.effectFlags);
        hashInt(hash, static_cast<std::int64_t>(function.switchability));
        for (const auto& site : function.delegationSites) {
            hashInt(hash, site.sourcePc);
            hashInt(hash, site.initPc);
            hashInt(hash, site.rangePc);
            hashInt(hash, site.branchPc);
            hashInt(hash, site.loadPc);
            hashInt(hash, site.yieldPc);
            hashInt(hash, site.backEdgePc);
            hashInt(hash, site.exitPc);
            hashInt(hash, site.sourceSlot);
            hashInt(hash, site.indexSlot);
            hashInt(hash, site.valueSlot);
        }
        for (const auto& block : function.blocks) {
            hashString(hash, block.id);
            for (const auto& operation : block.operations) {
                hashInt(hash, static_cast<std::int64_t>(operation.operation));
                hashInt(hash, static_cast<std::int64_t>(operation.opcode));
                hashInt(hash, operation.a);
                hashInt(hash, operation.b);
                hashInt(hash, operation.i64);
                hashString(hash, operation.text);
                hashString(hash, operation.source.file);
                hashInt(hash, operation.source.line);
                hashInt(hash, operation.source.column);
                hashInt(hash, static_cast<std::int64_t>(operation.callKind));
                hashString(hash, operation.targetId);
                hashString(hash, operation.callableBindingId);
                hashString(hash, operation.closureTargetFunctionId);
                hashInt(hash, operation.closureCaptureCount);
                hashInt(hash, operation.delegatedGeneratorRange ? 1 : 0);
                hashInt(hash, operation.effectFlags);
            }
            for (const auto& successor : block.successors)
                hashString(hash, successor);
        }
        for (const auto& local : function.locals) {
            hashString(hash, local.name);
            hashInt(hash, local.slot);
            hashInt(hash, static_cast<std::int64_t>(local.kind));
            hashType(hash, local.type);
            hashInt(hash, local.startOperation);
            hashInt(hash, local.endOperation);
        }
        for (const auto& safepoint : function.safepoints) {
            hashString(hash, safepoint.id);
            hashInt(hash, safepoint.operationIndex);
            hashString(hash, safepoint.resumeLabel);
            hashString(hash, safepoint.source.file);
            hashInt(hash, safepoint.source.line);
            hashInt(hash, safepoint.source.column);
            for (const auto slot : safepoint.canonicalSlots)
                hashInt(hash, slot);
        }
        for (const auto& region : function.exceptionRegions) {
            hashString(hash, region.id);
            hashInt(hash, region.startOperation);
            hashInt(hash, region.endOperation);
            hashInt(hash, region.handlerOperation);
            hashString(hash, region.handlerBlockId);
            hashString(hash, region.catchType);
            hashString(hash, region.source.file);
            hashInt(hash, region.source.line);
            hashInt(hash, region.source.column);
        }
    }
    return hashToString(hash);
}

ScriptResult validateTypedIr(const TypedIrModule& module)
{
    if (module.typedIrVersion != kTypedIrVersion
        || module.semanticAbiVersion != kSemanticAbiVersion) {
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrVersionMismatch,
            core::pmr::String(
                "unsupported typed IR or semantic ABI version", &core::defaultCpuMemoryResource()));
    }
    if (module.moduleId.empty())
        return invalid(
            core::pmr::String("typed IR module id is empty", &core::defaultCpuMemoryResource()));
    if (module.moduleVersion == 0 || module.globalCount < 0)
        return invalid(core::pmr::String("typed IR module version or global count is invalid",
            &core::defaultCpuMemoryResource()));
    if (!module.moduleInitFunctionId.empty()
        && std::none_of(
            module.functions.begin(), module.functions.end(), [&module](const auto& function) {
                return function.id == module.moduleInitFunctionId;
            })) {
        return invalid(core::pmr::String(
            "typed IR module initializer target is unresolved", &core::defaultCpuMemoryResource()));
    }
    for (std::size_t index = 0; index < module.globals.size(); ++index) {
        const auto& global = module.globals[index];
        if (global.name.empty() || global.slot < 0 || global.slot >= module.globalCount) {
            return invalid(core::pmr::String(
                "typed IR global slot is invalid", &core::defaultCpuMemoryResource()));
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (module.globals[other].name == global.name
                || module.globals[other].slot == global.slot) {
                return invalid(core::pmr::String(
                    "typed IR global slots are duplicated", &core::defaultCpuMemoryResource()));
            }
        }
    }
    for (std::size_t index = 0; index < module.callableBindings.size(); ++index) {
        const auto& binding = module.callableBindings[index];
        if (binding.bindingId.empty() || binding.targetId.empty() || binding.signatureHash.empty()
            || binding.fixedArity < 0 || binding.lifetimeVersion == 0
            || (binding.generatorBridge && binding.origin != TypedIrCallableOrigin::ScriptFunction)
            || (binding.origin != TypedIrCallableOrigin::HostCapability
                && binding.origin != TypedIrCallableOrigin::ReflectionMember
                && binding.origin != TypedIrCallableOrigin::ScriptFunction)) {
            return invalid(core::pmr::String(
                "typed IR callable binding is incomplete", &core::defaultCpuMemoryResource()));
        }
        for (std::size_t other = index + 1; other < module.callableBindings.size(); ++other) {
            if (binding.bindingId == module.callableBindings[other].bindingId) {
                return invalid(core::pmr::String("typed IR callable binding ids are duplicated",
                    &core::defaultCpuMemoryResource()));
            }
        }
    }
    for (std::size_t index = 0; index < module.scriptClassSchemas.size(); ++index) {
        const auto& schema = module.scriptClassSchemas[index];
        if (schema.className.empty())
            return invalid(core::pmr::String(
                "typed IR script class schema has no name", &core::defaultCpuMemoryResource()));
        for (std::size_t other = index + 1; other < module.scriptClassSchemas.size(); ++other) {
            if (schema.className == module.scriptClassSchemas[other].className)
                return invalid(
                    core::pmr::String("typed IR script class schema names are duplicated",
                        &core::defaultCpuMemoryResource()));
        }
        for (std::size_t field = 0; field < schema.fields.size(); ++field) {
            if (schema.fields[field].empty())
                return invalid(core::pmr::String(
                    "typed IR script class field is empty", &core::defaultCpuMemoryResource()));
            for (std::size_t other = field + 1; other < schema.fields.size(); ++other) {
                if (schema.fields[field] == schema.fields[other])
                    return invalid(core::pmr::String("typed IR script class fields are duplicated",
                        &core::defaultCpuMemoryResource()));
            }
        }
        for (const auto& method : schema.methods) {
            if (method.first.empty() || method.second.empty())
                return invalid(
                    core::pmr::String("typed IR script class method metadata is incomplete",
                        &core::defaultCpuMemoryResource()));
        }
    }
    const auto findCallableBinding =
        [&module](const core::pmr::String& bindingId) -> const TypedIrCallableBinding* {
        for (const auto& binding : module.callableBindings) {
            if (binding.bindingId == bindingId)
                return &binding;
        }
        return nullptr;
    };
    const auto isExternalScriptFunctionTarget = [&module](const core::pmr::String& targetId) {
        constexpr const char* prefix = "arp-fn:";
        if (targetId.rfind(prefix, 0) != 0)
            return false;
        const auto separator = targetId.find('/');
        if (separator == core::pmr::String::npos || separator == sizeof("arp-fn:") - 1u
            || separator + 1u >= targetId.size())
            return false;
        const auto dependency =
            targetId.substr(sizeof("arp-fn:") - 1u, separator - (sizeof("arp-fn:") - 1u));
        return dependency != module.moduleId
            && std::find(module.dependencies.begin(), module.dependencies.end(), dependency)
            != module.dependencies.end();
    };
    if (module.functions.empty())
        return invalid(core::pmr::String(
            "typed IR module has no functions", &core::defaultCpuMemoryResource()));

    for (const auto& binding : module.callableBindings) {
        if (binding.origin != TypedIrCallableOrigin::ScriptFunction)
            continue;
        const auto target = std::find_if(
            module.functions.begin(), module.functions.end(), [&binding](const auto& function) {
                return function.id == binding.targetId;
            });
        const bool localTargetValid = target != module.functions.end()
            && target->signature.variadic == false && target->captureCount == 0
            && target->parameterCount == binding.fixedArity
            && typedIrSignatureHash(target->signature) == binding.signatureHash;
        const bool externalTargetValid =
            target == module.functions.end() && isExternalScriptFunctionTarget(binding.targetId);
        if (!localTargetValid && !externalTargetValid) {
            return invalid(core::pmr::String(
                "script function callable binding does not match a local function or declared external dependency",
                &core::defaultCpuMemoryResource()));
        }
    }

    for (std::size_t i = 0; i < module.functions.size(); ++i) {
        const auto& function = module.functions[i];
        if (function.name.empty() || function.id.empty())
            return invalid(core::pmr::String(
                "typed IR function has no stable identity", &core::defaultCpuMemoryResource()));
        if (function.parameterCount < 0 || function.localCount < function.parameterCount
            || function.captureCount < 0 || function.captureCount > function.localCount
            || function.maxStack < 0)
            return invalid(core::pmr::String("typed IR function storage metadata is invalid",
                &core::defaultCpuMemoryResource()));
        if (function.id != makeTypedIrFunctionId(module.moduleId, function.name))
            return invalid(
                core::pmr::String("typed IR function id is not derived from its module and name",
                    &core::defaultCpuMemoryResource()));
        if (function.signature.fixedParameterCount < 0
            || function.signature.fixedParameterCount
                > static_cast<int>(function.signature.parameters.size()))
            return invalid(core::pmr::String("typed IR function has an invalid parameter contract",
                &core::defaultCpuMemoryResource()));
        if (!function.signature.variadic
            && function.signature.fixedParameterCount
                != static_cast<int>(function.signature.parameters.size()))
            return invalid(
                core::pmr::String("typed IR fixed parameter count does not match signature",
                    &core::defaultCpuMemoryResource()));
        if (function.signature.variadic
            && function.signature.fixedParameterCount
                != static_cast<int>(function.signature.parameters.size()))
            return invalid(core::pmr::String("typed IR variadic function has a non-fixed parameter",
                &core::defaultCpuMemoryResource()));
        for (std::size_t j = i + 1; j < module.functions.size(); ++j) {
            if (function.id == module.functions[j].id || function.name == module.functions[j].name)
                return invalid(core::pmr::String(
                    "typed IR function ids must be unique", &core::defaultCpuMemoryResource()));
        }

        if (function.blocks.empty())
            return invalid(core::pmr::String(
                "typed IR function has no basic block", &core::defaultCpuMemoryResource()));
        for (std::size_t b = 0; b < function.blocks.size(); ++b) {
            if (function.blocks[b].id.empty())
                return invalid(core::pmr::String(
                    "typed IR basic block id is empty", &core::defaultCpuMemoryResource()));
            for (const auto& operation : function.blocks[b].operations) {
                if (operation.opcode >= TypedIrOpcode::OpcodeCount)
                    return invalid(core::pmr::String(
                        "typed IR opcode is invalid", &core::defaultCpuMemoryResource()));
                const auto& descriptor = typedIrOperationDescriptor(operation.opcode);
                if (operation.operation != descriptor.operation
                    || operation.callKind != descriptor.callKind
                    || (operation.effectFlags & descriptor.requiredEffects)
                        != descriptor.requiredEffects) {
                    return invalid(core::pmr::String(
                        "typed IR operation metadata disagrees with opcode descriptor",
                        &core::defaultCpuMemoryResource()));
                }
            }
            for (const auto& successor : function.blocks[b].successors) {
                bool found = false;
                for (const auto& candidate : function.blocks) {
                    if (candidate.id == successor) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return invalid(core::pmr::String(
                        "typed IR block successor is unknown", &core::defaultCpuMemoryResource()));
            }
            for (std::size_t other = b + 1; other < function.blocks.size(); ++other) {
                if (function.blocks[b].id == function.blocks[other].id)
                    return invalid(core::pmr::String("typed IR basic block ids must be unique",
                        &core::defaultCpuMemoryResource()));
            }
        }
        for (std::size_t l = 0; l < function.locals.size(); ++l) {
            const auto& local = function.locals[l];
            if (local.slot < 0 || local.slot >= function.localCount || local.startOperation < 0
                || local.endOperation < local.startOperation)
                return invalid(core::pmr::String(
                    "typed IR local metadata is invalid", &core::defaultCpuMemoryResource()));
            for (std::size_t other = l + 1; other < function.locals.size(); ++other) {
                if (local.slot == function.locals[other].slot)
                    return invalid(core::pmr::String(
                        "typed IR local slots must be unique", &core::defaultCpuMemoryResource()));
            }
        }
        int operationCount = 0;
        int suspendOperationCount = 0;
        int delegatedRangeCount = 0;
        for (const auto& block : function.blocks) {
            operationCount += static_cast<int>(block.operations.size());
            for (const auto& operation : block.operations) {
                if (operation.operation == TypedIrOperation::Suspend)
                    ++suspendOperationCount;
                if (operation.delegatedGeneratorRange) {
                    if (operation.opcode != TypedIrOpcode::RangeNext)
                        return invalid(core::pmr::String(
                            "typed IR delegation metadata must identify RangeNext",
                            &core::defaultCpuMemoryResource()));
                    ++delegatedRangeCount;
                }
            }
        }
        if (function.hasYieldFrom != (delegatedRangeCount != 0)
            || function.delegationSites.size() != static_cast<std::size_t>(delegatedRangeCount)
            || (delegatedRangeCount != 0 && !function.isGenerator)) {
            return invalid(
                core::pmr::String("typed IR yield-from function and operation metadata disagree",
                    &core::defaultCpuMemoryResource()));
        }
        core::pmr::Vector<const TypedIrOperationRecord*> flattened{
            &core::defaultCpuMemoryResource()};
        flattened.reserve(static_cast<std::size_t>(operationCount));
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations)
                flattened.push_back(&operation);
        }
        for (std::size_t siteIndex = 0u; siteIndex < function.delegationSites.size(); ++siteIndex) {
            const auto& site = function.delegationSites[siteIndex];
            const auto inBounds = [&](int pc, bool allowEnd = false) {
                return pc >= 0 && (allowEnd ? pc <= operationCount : pc < operationCount);
            };
            if (!inBounds(site.sourcePc) || !inBounds(site.initPc) || !inBounds(site.rangePc)
                || !inBounds(site.branchPc) || !inBounds(site.loadPc) || !inBounds(site.yieldPc)
                || !inBounds(site.backEdgePc) || !inBounds(site.exitPc, true)
                || !(site.sourcePc < site.initPc && site.initPc < site.rangePc
                    && site.rangePc < site.branchPc && site.branchPc < site.loadPc
                    && site.loadPc < site.yieldPc && site.yieldPc < site.backEdgePc
                    && site.backEdgePc < site.exitPc)
                || flattened[static_cast<std::size_t>(site.initPc)]->opcode
                    != TypedIrOpcode::RangeInit
                || flattened[static_cast<std::size_t>(site.rangePc)]->opcode
                    != TypedIrOpcode::RangeNext
                || !flattened[static_cast<std::size_t>(site.rangePc)]->delegatedGeneratorRange
                || flattened[static_cast<std::size_t>(site.branchPc)]->opcode
                    != TypedIrOpcode::JumpIfFalse
                || flattened[static_cast<std::size_t>(site.loadPc)]->opcode
                    != TypedIrOpcode::LoadLocal
                || flattened[static_cast<std::size_t>(site.yieldPc)]->opcode != TypedIrOpcode::Yield
                || flattened[static_cast<std::size_t>(site.backEdgePc)]->opcode
                    != TypedIrOpcode::Jump
                || flattened[static_cast<std::size_t>(site.branchPc)]->a != site.exitPc
                || flattened[static_cast<std::size_t>(site.loadPc)]->a != site.valueSlot
                || flattened[static_cast<std::size_t>(site.backEdgePc)]->a != site.rangePc
                || flattened[static_cast<std::size_t>(site.rangePc)]->a != site.sourceSlot
                || flattened[static_cast<std::size_t>(site.rangePc)]->b != site.indexSlot
                || flattened[static_cast<std::size_t>(site.initPc)]->a != site.sourceSlot
                || flattened[static_cast<std::size_t>(site.initPc)]->b != site.indexSlot
                || site.sourceSlot < 0 || site.sourceSlot >= function.localCount
                || site.indexSlot < 0 || site.indexSlot >= function.localCount || site.valueSlot < 0
                || site.valueSlot >= function.localCount) {
                return invalid(core::pmr::String("typed IR delegation site metadata is invalid",
                    &core::defaultCpuMemoryResource()));
            }
            const auto packed =
                static_cast<std::uint64_t>(flattened[static_cast<std::size_t>(site.rangePc)]->i64);
            const int keySlot = static_cast<int>(static_cast<std::int32_t>(packed >> 32u));
            const int valueSlot = static_cast<int>(static_cast<std::int32_t>(packed & 0xffffffffu));
            if (keySlot != site.valueSlot || valueSlot != site.valueSlot) {
                return invalid(core::pmr::String("typed IR delegation range slots are invalid",
                    &core::defaultCpuMemoryResource()));
            }
            for (std::size_t other = 0u; other < siteIndex; ++other) {
                if (function.delegationSites[other].rangePc == site.rangePc
                    || function.delegationSites[other].yieldPc == site.yieldPc) {
                    return invalid(
                        core::pmr::String("typed IR delegation site metadata is duplicated",
                            &core::defaultCpuMemoryResource()));
                }
            }
        }
        for (std::size_t s = 0; s < function.safepoints.size(); ++s) {
            const auto& safepoint = function.safepoints[s];
            if (safepoint.id.empty() || safepoint.operationIndex < 0
                || safepoint.operationIndex >= operationCount || safepoint.resumeLabel.empty()
                || !safepoint.source.valid())
                return invalid(core::pmr::String(
                    "typed IR safepoint is incomplete", &core::defaultCpuMemoryResource()));
            int operationIndex = 0;
            const TypedIrOperationRecord* operationAtSafepoint = nullptr;
            const TypedIrBasicBlock* blockAtSafepoint = nullptr;
            int blockOperationOffset = -1;
            for (const auto& block : function.blocks) {
                for (std::size_t blockOffset = 0; blockOffset < block.operations.size();
                    ++blockOffset) {
                    const auto& operation = block.operations[blockOffset];
                    if (operationIndex == safepoint.operationIndex) {
                        operationAtSafepoint = &operation;
                        blockAtSafepoint = &block;
                        blockOperationOffset = static_cast<int>(blockOffset);
                    }
                    ++operationIndex;
                }
            }
            if (operationAtSafepoint == nullptr
                || operationAtSafepoint->operation != TypedIrOperation::Suspend)
                return invalid(
                    core::pmr::String("typed IR safepoint does not identify a suspend operation",
                        &core::defaultCpuMemoryResource()));
            core::pmr::String expectedResumeLabel(
                blockAtSafepoint->id, &core::defaultCpuMemoryResource());
            expectedResumeLabel += ":";
            expectedResumeLabel += std::to_string(blockOperationOffset);
            if (safepoint.resumeLabel != expectedResumeLabel
                || safepoint.source.file != operationAtSafepoint->source.file
                || safepoint.source.line != operationAtSafepoint->source.line
                || safepoint.source.column != operationAtSafepoint->source.column)
                return invalid(core::pmr::String(
                    "typed IR safepoint resume label or source map is inconsistent",
                    &core::defaultCpuMemoryResource()));
            for (const auto slot : safepoint.canonicalSlots) {
                if (slot < 0 || slot >= function.localCount)
                    return invalid(core::pmr::String(
                        "typed IR safepoint slot is invalid", &core::defaultCpuMemoryResource()));
                const auto occurrences = std::count(
                    safepoint.canonicalSlots.begin(), safepoint.canonicalSlots.end(), slot);
                if (occurrences != 1)
                    return invalid(
                        core::pmr::String("typed IR safepoint canonical slots are duplicated",
                            &core::defaultCpuMemoryResource()));
            }
            if (static_cast<int>(safepoint.canonicalSlots.size()) != function.localCount)
                return invalid(core::pmr::String("typed IR safepoint does not map every local slot",
                    &core::defaultCpuMemoryResource()));
            for (std::size_t other = s + 1; other < function.safepoints.size(); ++other) {
                if (safepoint.id == function.safepoints[other].id)
                    return invalid(core::pmr::String("typed IR safepoint ids must be unique",
                        &core::defaultCpuMemoryResource()));
            }
        }
        if (function.switchability == TypedIrSwitchability::Safepoint
            && (function.safepoints.empty()
                || static_cast<int>(function.safepoints.size()) != suspendOperationCount))
            return invalid(core::pmr::String("safepoint-switchable function has no safepoint map",
                &core::defaultCpuMemoryResource()));
        if (function.switchability == TypedIrSwitchability::ArbitraryPc) {
            for (const auto& block : function.blocks) {
                for (const auto& operation : block.operations) {
                    if (!operation.source.valid())
                        return invalid(core::pmr::String(
                            "arbitrary-PC function has an incomplete operation source map",
                            &core::defaultCpuMemoryResource()));
                }
            }
        }
        const auto scalarStack = analyzeTypedIrScalarStack(function);
        if (scalarStack.status == TypedIrScalarStackStatus::Invalid)
            return invalid(
                core::pmr::String("typed IR scalar stack contract exceeds declared maxStack",
                    &core::defaultCpuMemoryResource()));
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations) {
                const bool requiresFunctionTarget = operation.opcode == TypedIrOpcode::Call
                    || operation.opcode == TypedIrOpcode::MakeFuncValue
                    || operation.opcode == TypedIrOpcode::MakeClosure
                    || operation.opcode == TypedIrOpcode::MakeMethodValue;
                if (requiresFunctionTarget) {
                    const auto target = std::find_if(module.functions.begin(),
                        module.functions.end(),
                        [&operation](const auto& candidate) {
                            return candidate.id == operation.targetId;
                        });
                    if (operation.targetId.empty() || target == module.functions.end()) {
                        return invalid(core::pmr::String(
                            "typed IR function reference has an unresolved target id",
                            &core::defaultCpuMemoryResource()));
                    }
                }
                if (operation.opcode == TypedIrOpcode::CallHost
                    && (operation.targetId.empty() || operation.callableBindingId.empty())) {
                    return invalid(core::pmr::String(
                        "typed IR host call requires stable target and binding ids",
                        &core::defaultCpuMemoryResource()));
                }
                if (!operation.callableBindingId.empty()) {
                    const auto* binding = findCallableBinding(operation.callableBindingId);
                    const bool callUse = operation.opcode == TypedIrOpcode::CallValue
                        && operation.b == (binding != nullptr ? binding->fixedArity : -1);
                    const bool valueUse = operation.opcode == TypedIrOpcode::LoadField
                        || operation.opcode == TypedIrOpcode::LoadModuleExport;
                    if (binding == nullptr || (!callUse && !valueUse)) {
                        core::pmr::String diagnostic(
                            "typed IR callable binding use is invalid: function=",
                            &core::defaultCpuMemoryResource());
                        diagnostic += function.id;
                        diagnostic += ", operation=";
                        diagnostic += typedIrOpcodeName(operation.opcode);
                        diagnostic += ", binding=";
                        diagnostic += operation.callableBindingId;
                        return invalid(std::move(diagnostic));
                    }
                }
                if (!operation.closureTargetFunctionId.empty() || operation.closureCaptureCount >= 0
                    || operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount) {
                    const auto target = std::find_if(module.functions.begin(),
                        module.functions.end(),
                        [&operation](const auto& candidate) {
                            return candidate.id == operation.closureTargetFunctionId;
                        });
                    if (operation.opcode != TypedIrOpcode::CallValue
                        || !operation.callableBindingId.empty()
                        || operation.closureTargetFunctionId.empty()
                        || target == module.functions.end()
                        || (operation.closureCaptureCount == kScriptMethodProvenanceCaptureCount
                                ? (target->captureCount != 0
                                      || target->parameterCount != operation.b + 1)
                                : (operation.closureCaptureCount < 0
                                      || target->captureCount != operation.closureCaptureCount
                                      || target->parameterCount != operation.b))) {
                        return invalid(
                            core::pmr::String("typed IR closure provenance use is invalid",
                                &core::defaultCpuMemoryResource()));
                    }
                }
            }
        }
        if (function.exceptionRegions.empty()) {
            const auto scalarControlFlow = analyzeTypedIrScalarControlFlow(function);
            if (scalarControlFlow.status == TypedIrScalarStackStatus::Invalid) {
                core::pmr::String diagnostic("typed IR scalar control-flow contract is invalid: ",
                    &core::defaultCpuMemoryResource());
                diagnostic += function.id;
                diagnostic += " pc=";
                diagnostic += std::to_string(scalarControlFlow.failureOperationIndex);
                diagnostic += " opcode=";
                diagnostic += scalarControlFlow.failureOperation;
                diagnostic += " reason=";
                diagnostic += scalarControlFlow.failureReason;
                return ScriptResult::failure(ScriptDiagnostic(ScriptDiagnosticCode::TypedIrInvalid,
                    std::move(diagnostic),
                    scalarControlFlow.failureSource.file,
                    scalarControlFlow.failureSource.line,
                    scalarControlFlow.failureSource.column));
            }
        }
        for (std::size_t r = 0; r < function.exceptionRegions.size(); ++r) {
            const auto& region = function.exceptionRegions[r];
            if (region.id.empty() || region.handlerBlockId.empty() || region.startOperation < 0
                || region.endOperation <= region.startOperation
                || region.endOperation > operationCount || region.handlerOperation < 0
                || region.handlerOperation >= operationCount || !region.source.valid())
                return invalid(core::pmr::String(
                    "typed IR exception region is incomplete", &core::defaultCpuMemoryResource()));
            bool handlerFound = false;
            int blockOperation = 0;
            for (const auto& block : function.blocks) {
                if (block.id == region.handlerBlockId) {
                    handlerFound = blockOperation == region.handlerOperation;
                    break;
                }
                blockOperation += static_cast<int>(block.operations.size());
            }
            if (!handlerFound)
                return invalid(core::pmr::String(
                    "typed IR exception handler block or operation is inconsistent",
                    &core::defaultCpuMemoryResource()));
            for (std::size_t other = r + 1; other < function.exceptionRegions.size(); ++other) {
                if (region.id == function.exceptionRegions[other].id)
                    return invalid(core::pmr::String("typed IR exception region ids must be unique",
                        &core::defaultCpuMemoryResource()));
                const auto& candidate = function.exceptionRegions[other];
                const bool disjoint = region.endOperation <= candidate.startOperation
                    || candidate.endOperation <= region.startOperation;
                const bool contains = region.startOperation <= candidate.startOperation
                    && candidate.endOperation <= region.endOperation;
                const bool contained = candidate.startOperation <= region.startOperation
                    && region.endOperation <= candidate.endOperation;
                if (!disjoint && !contains && !contained)
                    return invalid(
                        core::pmr::String("typed IR exception regions overlap without nesting",
                            &core::defaultCpuMemoryResource()));
            }
        }
        // Exception metadata is consumed by artifact inspection and future
        // native lowering, while the interpreter executes Push/PopHandler.
        // Keep the two representations one-to-one so they cannot diverge.
        core::pmr::Vector<std::size_t> regionStack{&core::defaultCpuMemoryResource()};
        auto matchedRegionsAllocator =
            core::pmr::Vector<bool>::allocator_type(&core::defaultCpuMemoryResource());
        core::pmr::Vector<bool> matchedRegions(matchedRegionsAllocator);
        matchedRegions.resize(function.exceptionRegions.size(), false);
        int exceptionOperation = 0;
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations) {
                if (operation.opcode == TypedIrOpcode::PushHandler) {
                    std::size_t matched = function.exceptionRegions.size();
                    for (std::size_t r = 0; r < function.exceptionRegions.size(); ++r) {
                        const auto& region = function.exceptionRegions[r];
                        if (region.startOperation == exceptionOperation + 1
                            && region.handlerOperation == operation.a) {
                            matched = r;
                            break;
                        }
                    }
                    if (matched == function.exceptionRegions.size() || matchedRegions[matched])
                        return invalid(
                            core::pmr::String("typed IR PushHandler has no unique exception region",
                                &core::defaultCpuMemoryResource()));
                    matchedRegions[matched] = true;
                    regionStack.push_back(matched);
                } else if (operation.opcode == TypedIrOpcode::PopHandler && operation.a >= 0) {
                    if (regionStack.empty())
                        return invalid(
                            core::pmr::String("typed IR PopHandler has no matching PushHandler",
                                &core::defaultCpuMemoryResource()));
                    const auto regionIndex = regionStack.back();
                    regionStack.pop_back();
                    if (function.exceptionRegions[regionIndex].endOperation != exceptionOperation)
                        return invalid(core::pmr::String(
                            "typed IR exception region does not end at its PopHandler",
                            &core::defaultCpuMemoryResource()));
                }
                ++exceptionOperation;
            }
        }
        if (!regionStack.empty())
            return invalid(core::pmr::String("typed IR PushHandler has no matching PopHandler",
                &core::defaultCpuMemoryResource()));
        for (const bool matched : matchedRegions) {
            if (!matched)
                return invalid(core::pmr::String("typed IR exception region has no PushHandler",
                    &core::defaultCpuMemoryResource()));
        }

        // Validate the dynamic handler stack on normal CFG edges. Exception
        // dispatch itself enters a handler after removing its own region, so
        // seed each handler with precisely its enclosing region chain.
        core::pmr::Vector<const TypedIrOperationRecord*> operations{
            &core::defaultCpuMemoryResource()};
        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations)
                operations.push_back(&operation);
        }
        core::pmr::Vector<core::pmr::Vector<int>> handlerStates{&core::defaultCpuMemoryResource()};
        handlerStates.resize(operations.size());
        core::pmr::Vector<std::uint8_t> handlerStatePresent(
            operations.size(), 0u, &core::defaultCpuMemoryResource());
        core::pmr::Vector<std::uint8_t> handlerQueued(
            operations.size(), 0u, &core::defaultCpuMemoryResource());
        core::pmr::Vector<std::size_t> handlerWorklist{&core::defaultCpuMemoryResource()};
        auto mergeHandlerState = [&](std::size_t target,
                                     const core::pmr::Vector<int>& state) -> bool {
            if (target >= operations.size())
                return false;
            if (!handlerStatePresent[target]) {
                handlerStates[target] = state;
                handlerStatePresent[target] = 1u;
                if (!handlerQueued[target]) {
                    handlerWorklist.push_back(target);
                    handlerQueued[target] = 1u;
                }
                return true;
            }
            if (handlerStates[target] != state)
                return false;
            return true;
        };
        core::pmr::Vector<int> emptyHandlerState{&core::defaultCpuMemoryResource()};
        if (!mergeHandlerState(0u, emptyHandlerState))
            return invalid(core::pmr::String("typed IR exception CFG has no entry operation",
                &core::defaultCpuMemoryResource()));
        for (std::size_t regionIndex = 0; regionIndex < function.exceptionRegions.size();
            ++regionIndex) {
            const auto& region = function.exceptionRegions[regionIndex];
            core::pmr::Vector<int> seed{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<int> ancestors{&core::defaultCpuMemoryResource()};
            int child = static_cast<int>(regionIndex);
            while (child >= 0) {
                int parent = -1;
                for (std::size_t candidateIndex = 0;
                    candidateIndex < function.exceptionRegions.size();
                    ++candidateIndex) {
                    if (candidateIndex == static_cast<std::size_t>(child))
                        continue;
                    const auto& candidate = function.exceptionRegions[candidateIndex];
                    const auto& nested = function.exceptionRegions[static_cast<std::size_t>(child)];
                    if (candidate.startOperation <= nested.startOperation
                        && nested.endOperation <= candidate.endOperation
                        && (parent < 0
                            || candidate.startOperation
                                > function.exceptionRegions[static_cast<std::size_t>(parent)]
                                    .startOperation))
                        parent = static_cast<int>(candidateIndex);
                }
                if (parent >= 0)
                    ancestors.push_back(parent);
                child = parent;
            }
            for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
                seed.push_back(*it);
            if (!mergeHandlerState(static_cast<std::size_t>(region.handlerOperation), seed))
                return invalid(
                    core::pmr::String("typed IR exception handlers have incompatible entry stacks",
                        &core::defaultCpuMemoryResource()));
        }
        while (!handlerWorklist.empty()) {
            const auto pc = handlerWorklist.back();
            handlerWorklist.pop_back();
            handlerQueued[pc] = 0u;
            auto state = handlerStates[pc];
            const auto& operation = *operations[pc];
            if (operation.opcode == TypedIrOpcode::PushHandler) {
                int regionIndex = -1;
                for (std::size_t index = 0; index < function.exceptionRegions.size(); ++index) {
                    const auto& region = function.exceptionRegions[index];
                    if (region.startOperation == static_cast<int>(pc) + 1
                        && region.handlerOperation == operation.a) {
                        regionIndex = static_cast<int>(index);
                        break;
                    }
                }
                if (regionIndex < 0)
                    return invalid(core::pmr::String(
                        "typed IR exception CFG cannot resolve PushHandler region",
                        &core::defaultCpuMemoryResource()));
                state.push_back(regionIndex);
            } else if (operation.opcode == TypedIrOpcode::PopHandler && operation.a >= 0) {
                if (state.empty())
                    return invalid(
                        core::pmr::String("typed IR exception CFG handler stack underflow",
                            &core::defaultCpuMemoryResource()));
                state.pop_back();
            }
            const bool returns = operation.opcode == TypedIrOpcode::Return
                || operation.opcode == TypedIrOpcode::ReturnInt
                || operation.opcode == TypedIrOpcode::Throw;
            const bool unconditionalJump = operation.opcode == TypedIrOpcode::Jump;
            const bool conditionalJump = operation.opcode == TypedIrOpcode::JumpIfFalse
                || operation.opcode == TypedIrOpcode::JumpIfTrue
                || operation.opcode == TypedIrOpcode::JumpIfLocalIntLtConstFalse;
            if (unconditionalJump || conditionalJump) {
                if (operation.a < 0 || operation.a > static_cast<int>(operations.size()))
                    return invalid(
                        core::pmr::String("typed IR exception CFG has an invalid jump target",
                            &core::defaultCpuMemoryResource()));
                if (operation.a < static_cast<int>(operations.size())
                    && !mergeHandlerState(static_cast<std::size_t>(operation.a), state))
                    return invalid(core::pmr::String(
                        "typed IR exception CFG has an incompatible jump handler stack",
                        &core::defaultCpuMemoryResource()));
            }
            if (!returns && !unconditionalJump) {
                if (pc + 1u >= operations.size()) {
                    if (operation.opcode == TypedIrOpcode::Yield
                        || operation.opcode == TypedIrOpcode::Await)
                        continue;
                    return invalid(core::pmr::String(
                        "typed IR exception CFG falls through past the final operation",
                        &core::defaultCpuMemoryResource()));
                }
                if (!mergeHandlerState(pc + 1u, state))
                    return invalid(
                        core::pmr::String("typed IR exception CFG has incompatible handler stacks",
                            &core::defaultCpuMemoryResource()));
            }
        }
    }

    if (module.fingerprint.empty())
        return invalid(core::pmr::String(
            "typed IR fingerprint is missing", &core::defaultCpuMemoryResource()));
    if (module.fingerprint != computeTypedIrFingerprint(module))
        return invalid(core::pmr::String("typed IR fingerprint does not match module contents",
            &core::defaultCpuMemoryResource()));
    return ScriptResult::success();
}

void annotateTypedIrClosureCallProvenance(TypedIrModule& module,
    TypedIrFunction& function,
    const core::pmr::Vector<ClosureProvenance>& initialGlobals);

void annotateModuleInitClosureCallProvenance(TypedIrModule& module)
{
    core::pmr::Vector<ClosureProvenance> globals(static_cast<std::size_t>(module.globalCount),
        ClosureProvenance{},
        &core::defaultCpuMemoryResource());
    const auto initializer = std::find_if(
        module.functions.begin(), module.functions.end(), [&module](const auto& function) {
            return function.id == module.moduleInitFunctionId;
        });
    const bool hasInitializer = initializer != module.functions.end();
    bool initializerProven = hasInitializer;
    if (hasInitializer) {
        const auto& init = *initializer;
        ClosureProvenanceState state;
        state.locals.resize(static_cast<std::size_t>(init.localCount));
        state.globals.resize(static_cast<std::size_t>(module.globalCount));
        auto pop = [&state](ClosureProvenance& value) {
            if (state.stack.empty())
                return false;
            value = state.stack.back();
            state.stack.pop_back();
            return true;
        };
        bool returned = false;
        for (const auto& block : init.blocks)
            for (const auto& op : block.operations) {
                if (returned) {
                    // The initializer analysis is intentionally linear. A dead
                    // tail must not manufacture an initialization fact.
                    initializerProven = false;
                } else if (op.opcode == TypedIrOpcode::MakeFuncValue
                    && functionIndexById(module, op.targetId) >= 0
                    && module
                            .functions[static_cast<std::size_t>(
                                functionIndexById(module, op.targetId))]
                            .captureCount
                        == 0)
                    state.stack.push_back({functionIndexById(module, op.targetId), 0});
                else if (op.opcode == TypedIrOpcode::LoadLocal && op.a >= 0
                    && op.a < init.localCount)
                    state.stack.push_back(state.locals[static_cast<std::size_t>(op.a)]);
                else if (op.opcode == TypedIrOpcode::LoadGlobal && op.a >= 0
                    && op.a < static_cast<int>(state.globals.size()))
                    state.stack.push_back(state.globals[static_cast<std::size_t>(op.a)]);
                else if (op.opcode == TypedIrOpcode::StoreLocal && op.a >= 0
                    && op.a < init.localCount) {
                    ClosureProvenance value;
                    if (!pop(value))
                        initializerProven = false;
                    else
                        state.locals[static_cast<std::size_t>(op.a)] = value;
                } else if (op.opcode == TypedIrOpcode::StoreGlobal && op.a >= 0
                    && op.a < static_cast<int>(state.globals.size())) {
                    ClosureProvenance value;
                    if (!pop(value))
                        initializerProven = false;
                    else
                        state.globals[static_cast<std::size_t>(op.a)] = value;
                } else if (op.opcode == TypedIrOpcode::LoadNil
                    || op.opcode == TypedIrOpcode::LoadInt) {
                    state.stack.push_back({});
                } else if (op.opcode == TypedIrOpcode::Return) {
                    for (int result = 0; result < op.a; ++result) {
                        ClosureProvenance ignored;
                        if (!pop(ignored))
                            initializerProven = false;
                    }
                    returned = true;
                } else {
                    initializerProven = false;
                }
                if (!initializerProven)
                    break;
            }
        if (initializerProven)
            globals = std::move(state.globals);
    }
    if (!initializerProven)
        std::fill(globals.begin(), globals.end(), ClosureProvenance{});
    for (std::size_t fi = 0; fi < module.functions.size(); ++fi) {
        if (!hasInitializer || module.functions[fi].id != module.moduleInitFunctionId)
            annotateTypedIrClosureCallProvenance(module, module.functions[fi], globals);
    }
}

// Shared Typed-IR closure provenance engine used after the source frontend
// materializes canonical functions.
void annotateTypedIrClosureCallProvenance(TypedIrModule& module,
    TypedIrFunction& function,
    const core::pmr::Vector<ClosureProvenance>& initialGlobals)
{
    if (function.localCount < 0 || function.captureCount < 0 || module.globalCount < 0)
        return;
    core::pmr::Vector<TypedIrOperationRecord*> operations{&core::defaultCpuMemoryResource()};
    for (auto& block : function.blocks)
        for (auto& operation : block.operations)
            operations.push_back(&operation);
    if (operations.empty())
        return;

    core::pmr::Vector<ClosureProvenanceState> states{&core::defaultCpuMemoryResource()};
    states.resize(operations.size());
    core::pmr::Vector<std::uint8_t> reached(
        operations.size(), 0u, &core::defaultCpuMemoryResource());
    core::pmr::Vector<int> pending{&core::defaultCpuMemoryResource()};
    states[0].locals.resize(static_cast<std::size_t>(function.localCount));
    states[0].globals.resize(static_cast<std::size_t>(module.globalCount));
    states[0].captures.resize(static_cast<std::size_t>(function.captureCount));
    if (initialGlobals.size() == states[0].globals.size())
        states[0].globals = initialGlobals;
    reached[0] = 1u;
    pending.push_back(0);
    auto propagate = [&](int target, const ClosureProvenanceState& state) {
        if (target < 0 || target >= static_cast<int>(operations.size()))
            return;
        const auto index = static_cast<std::size_t>(target);
        if (reached[index] == 0u) {
            states[index] = state;
            reached[index] = 1u;
            pending.push_back(target);
        } else if (mergeClosureProvenanceState(states[index], state)) {
            pending.push_back(target);
        }
    };
    while (!pending.empty()) {
        const int pc = pending.back();
        pending.pop_back();
        auto state = states[static_cast<std::size_t>(pc)];
        auto& operation = *operations[static_cast<std::size_t>(pc)];
        auto pop = [&](ClosureProvenance& value) {
            if (state.stack.empty())
                return false;
            value = state.stack.back();
            state.stack.pop_back();
            return true;
        };
        auto popUnknown = [&]() {
            ClosureProvenance ignored;
            return pop(ignored);
        };
        auto pushUnknown = [&]() {
            state.stack.push_back({});
        };
        const auto opcode = operation.opcode;
        bool valid = true;
        if (opcode == TypedIrOpcode::LoadLocal) {
            if (operation.a < 0 || operation.a >= function.localCount)
                valid = false;
            else
                state.stack.push_back(state.locals[static_cast<std::size_t>(operation.a)]);
        } else if (opcode == TypedIrOpcode::StoreLocal) {
            ClosureProvenance value;
            if (operation.a < 0 || operation.a >= function.localCount || !pop(value))
                valid = false;
            else
                state.locals[static_cast<std::size_t>(operation.a)] = value;
        } else if (opcode == TypedIrOpcode::LoadGlobal) {
            if (operation.a < 0 || operation.a >= module.globalCount)
                valid = false;
            else
                state.stack.push_back(state.globals[static_cast<std::size_t>(operation.a)]);
        } else if (opcode == TypedIrOpcode::StoreGlobal) {
            ClosureProvenance value;
            if (operation.a < 0 || operation.a >= module.globalCount || !pop(value))
                valid = false;
            else
                state.globals[static_cast<std::size_t>(operation.a)] = value;
        } else if (opcode == TypedIrOpcode::LoadCapture) {
            if (operation.a < 0 || operation.a >= function.captureCount)
                valid = false;
            else
                state.stack.push_back(state.captures[static_cast<std::size_t>(operation.a)]);
        } else if (opcode == TypedIrOpcode::StoreCapture) {
            ClosureProvenance value;
            if (operation.a < 0 || operation.a >= function.captureCount || !pop(value))
                valid = false;
            else
                state.captures[static_cast<std::size_t>(operation.a)] = value;
        } else if (opcode == TypedIrOpcode::MakeClosure) {
            for (int index = 0; valid && index < operation.b; ++index)
                valid = popUnknown();
            const int targetIndex = functionIndexById(module, operation.targetId);
            if (targetIndex < 0 || operation.b < 0)
                valid = false;
            if (valid)
                state.stack.push_back({targetIndex, operation.b});
        } else if (opcode == TypedIrOpcode::MakeFuncValue) {
            const int targetIndex = functionIndexById(module, operation.targetId);
            if (targetIndex < 0)
                valid = false;
            else
                state.stack.push_back({targetIndex, 0});
        } else if (opcode == TypedIrOpcode::MakeMethodValue) {
            const int targetIndex = functionIndexById(module, operation.targetId);
            if (!popUnknown() || targetIndex < 0)
                valid = false;
            else
                state.stack.push_back({targetIndex, 0, true});
        } else if (opcode == TypedIrOpcode::CallValue) {
            ClosureProvenance callee;
            if (operation.b < 0 || !pop(callee))
                valid = false;
            for (int index = 0; valid && index < operation.b; ++index)
                valid = popUnknown();
            if (valid && callee.known()) {
                const auto& target =
                    module.functions[static_cast<std::size_t>(callee.functionIndex)];
                if (callee.scriptMethod
                        ? (target.captureCount == 0 && target.parameterCount == operation.b + 1)
                        : (target.captureCount == callee.captureCount
                              && target.parameterCount == operation.b)) {
                    operation.closureTargetFunctionId = target.id;
                    operation.closureCaptureCount = callee.scriptMethod
                        ? kScriptMethodProvenanceCaptureCount
                        : callee.captureCount;
                }
            }
            if (valid) {
                pushUnknown();
                std::fill(state.globals.begin(), state.globals.end(), ClosureProvenance{});
                std::fill(state.captures.begin(), state.captures.end(), ClosureProvenance{});
            }
        } else if (opcode == TypedIrOpcode::LoadNil || opcode == TypedIrOpcode::LoadBool
            || opcode == TypedIrOpcode::LoadInt || opcode == TypedIrOpcode::LoadInt64
            || opcode == TypedIrOpcode::LoadFloat || opcode == TypedIrOpcode::LoadString
            || opcode == TypedIrOpcode::AddrLocal || opcode == TypedIrOpcode::AddrCapture
            || opcode == TypedIrOpcode::AddrGlobal || opcode == TypedIrOpcode::MakeRange) {
            pushUnknown();
        } else if (opcode == TypedIrOpcode::Deref || opcode == TypedIrOpcode::Neg
            || opcode == TypedIrOpcode::Not || opcode == TypedIrOpcode::ConvertInt
            || opcode == TypedIrOpcode::ConvertInt64 || opcode == TypedIrOpcode::ConvertFloat
            || opcode == TypedIrOpcode::ConvertString || opcode == TypedIrOpcode::ConvertBool
            || opcode == TypedIrOpcode::ConvertBytes || opcode == TypedIrOpcode::BuiltinLen) {
            valid = popUnknown();
            if (valid)
                pushUnknown();
        } else if (opcode == TypedIrOpcode::StoreIndirect || opcode == TypedIrOpcode::BuiltinAppend
            || opcode == TypedIrOpcode::BuiltinDelete) {
            valid = popUnknown() && popUnknown();
            if (valid && opcode == TypedIrOpcode::BuiltinAppend)
                pushUnknown();
        } else if (opcode == TypedIrOpcode::BuiltinClear) {
            valid = popUnknown();
        } else if (opcode == TypedIrOpcode::Add || opcode == TypedIrOpcode::Sub
            || opcode == TypedIrOpcode::Mul || opcode == TypedIrOpcode::Div
            || opcode == TypedIrOpcode::Mod || opcode == TypedIrOpcode::Pow
            || opcode == TypedIrOpcode::FloorDiv || opcode == TypedIrOpcode::BitAnd
            || opcode == TypedIrOpcode::BitOr || opcode == TypedIrOpcode::BitXor
            || opcode == TypedIrOpcode::BitClear || opcode == TypedIrOpcode::ShiftLeft
            || opcode == TypedIrOpcode::ShiftRight || opcode == TypedIrOpcode::CmpEq
            || opcode == TypedIrOpcode::CmpNe || opcode == TypedIrOpcode::CmpLt
            || opcode == TypedIrOpcode::CmpLe || opcode == TypedIrOpcode::CmpGt
            || opcode == TypedIrOpcode::CmpGe || opcode == TypedIrOpcode::IntAdd
            || opcode == TypedIrOpcode::IntSub || opcode == TypedIrOpcode::IntMul
            || opcode == TypedIrOpcode::IntDiv || opcode == TypedIrOpcode::IntMod
            || opcode == TypedIrOpcode::IntCmpEq || opcode == TypedIrOpcode::IntCmpNe
            || opcode == TypedIrOpcode::IntCmpLt || opcode == TypedIrOpcode::IntCmpLe
            || opcode == TypedIrOpcode::IntCmpGt || opcode == TypedIrOpcode::IntCmpGe) {
            valid = popUnknown() && popUnknown();
            if (valid)
                pushUnknown();
        } else if (opcode == TypedIrOpcode::LocalAddIntConst
            || opcode == TypedIrOpcode::LocalSubIntConst
            || opcode == TypedIrOpcode::LocalAddInt64Const
            || opcode == TypedIrOpcode::LocalSubInt64Const
            || opcode == TypedIrOpcode::LocalAddFloatConst
            || opcode == TypedIrOpcode::LocalSubFloatConst || opcode == TypedIrOpcode::LocalAddLocal
            || opcode == TypedIrOpcode::RangeInit || opcode == TypedIrOpcode::RangeNext) {
            if (opcode == TypedIrOpcode::RangeInit)
                valid = popUnknown();
            else if (opcode == TypedIrOpcode::RangeNext)
                pushUnknown();
        } else if (opcode == TypedIrOpcode::Call) {
            if (operation.b < 0)
                valid = false;
            for (int index = 0; valid && index < operation.b; ++index)
                valid = popUnknown();
            if (valid) {
                pushUnknown();
                std::fill(state.globals.begin(), state.globals.end(), ClosureProvenance{});
                std::fill(state.captures.begin(), state.captures.end(), ClosureProvenance{});
            }
        } else if (opcode == TypedIrOpcode::MakeList || opcode == TypedIrOpcode::MakeDict
            || opcode == TypedIrOpcode::MakeRangeFromStack) {
            const int count = opcode == TypedIrOpcode::MakeDict ? operation.b * 2 : operation.b;
            if (count < 0)
                valid = false;
            for (int index = 0; valid && index < count; ++index)
                valid = popUnknown();
            if (valid)
                pushUnknown();
        } else if (opcode == TypedIrOpcode::LoadIndex) {
            valid = popUnknown() && popUnknown();
            if (valid)
                pushUnknown();
        } else if (opcode == TypedIrOpcode::StoreIndex) {
            valid = popUnknown() && popUnknown() && popUnknown();
        } else if (opcode == TypedIrOpcode::LoadField) {
            valid = popUnknown();
            if (valid)
                pushUnknown();
        } else if (opcode == TypedIrOpcode::StoreField) {
            valid = popUnknown() && popUnknown();
        } else if (opcode == TypedIrOpcode::MakeInstance) {
            if (opcode == TypedIrOpcode::MakeInstance) {
                if (operation.b < 0)
                    valid = false;
                for (int index = 0; valid && index < operation.b; ++index)
                    valid = popUnknown();
            }
            pushUnknown();
        } else if (opcode == TypedIrOpcode::Pop) {
            valid = popUnknown();
        } else if (opcode == TypedIrOpcode::Return) {
            for (int index = 0; valid && index < operation.a; ++index)
                valid = popUnknown();
        } else if (opcode == TypedIrOpcode::ReturnInt || opcode == TypedIrOpcode::Throw
            || opcode == TypedIrOpcode::JumpIfFalse || opcode == TypedIrOpcode::JumpIfTrue) {
            valid = popUnknown();
        } else if (opcode == TypedIrOpcode::Await) {
            valid = popUnknown();
            if (valid) {
                pushUnknown();
                std::fill(state.globals.begin(), state.globals.end(), ClosureProvenance{});
                std::fill(state.captures.begin(), state.captures.end(), ClosureProvenance{});
            }
        } else if (opcode == TypedIrOpcode::Yield) {
            std::fill(state.globals.begin(), state.globals.end(), ClosureProvenance{});
            std::fill(state.captures.begin(), state.captures.end(), ClosureProvenance{});
        } else if (opcode != TypedIrOpcode::Jump) {
            return;
        }
        if (!valid || opcode == TypedIrOpcode::Return || opcode == TypedIrOpcode::ReturnInt
            || opcode == TypedIrOpcode::Throw)
            continue;
        if (opcode == TypedIrOpcode::Jump)
            propagate(operation.a, state);
        else if (opcode == TypedIrOpcode::JumpIfFalse || opcode == TypedIrOpcode::JumpIfTrue) {
            propagate(operation.a, state);
            propagate(pc + 1, state);
        } else
            propagate(pc + 1, state);
    }
}

TypedIrScalarStackAnalysis analyzeTypedIrScalarStack(const TypedIrFunction& function)
{
    TypedIrScalarStackAnalysis analysis;
    if (function.blocks.empty() || function.maxStack < 0)
        return analysis;

    int depth = 0;
    bool hasReturn = false;
    bool isFinalOperation = false;
    for (const auto& block : function.blocks) {
        if (!block.successors.empty())
            return analysis;
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            const auto& operation = block.operations[index];
            const auto opcode = operation.opcode;
            isFinalOperation =
                &block == &function.blocks.back() && index + 1u == block.operations.size();
            int pops = 0;
            int pushes = 0;
            bool returns = false;
            if (opcode == TypedIrOpcode::LoadBool || opcode == TypedIrOpcode::LoadInt
                || opcode == TypedIrOpcode::LoadLocal) {
                pushes = 1;
            } else if (opcode == TypedIrOpcode::StoreLocal || opcode == TypedIrOpcode::Pop) {
                pops = 1;
            } else if (opcode == TypedIrOpcode::IntAdd || opcode == TypedIrOpcode::IntSub
                || opcode == TypedIrOpcode::IntMul || opcode == TypedIrOpcode::IntDiv
                || opcode == TypedIrOpcode::IntMod || opcode == TypedIrOpcode::IntCmpEq
                || opcode == TypedIrOpcode::IntCmpNe || opcode == TypedIrOpcode::IntCmpLt
                || opcode == TypedIrOpcode::IntCmpLe || opcode == TypedIrOpcode::IntCmpGt
                || opcode == TypedIrOpcode::IntCmpGe || opcode == TypedIrOpcode::Pow
                || opcode == TypedIrOpcode::FloorDiv || opcode == TypedIrOpcode::BitAnd
                || opcode == TypedIrOpcode::BitOr || opcode == TypedIrOpcode::BitXor
                || opcode == TypedIrOpcode::BitClear || opcode == TypedIrOpcode::ShiftLeft
                || opcode == TypedIrOpcode::ShiftRight) {
                pops = 2;
                pushes = 1;
            } else if (opcode == TypedIrOpcode::Call) {
                if (operation.b < 0)
                    return TypedIrScalarStackAnalysis{TypedIrScalarStackStatus::Invalid, 0};
                pops = operation.b;
                pushes = 1;
            } else if (opcode == TypedIrOpcode::LocalAddIntConst
                || opcode == TypedIrOpcode::LocalSubIntConst
                || opcode == TypedIrOpcode::LocalAddLocal) {
                // These mutate locals and do not touch the operand stack.
            } else if (opcode == TypedIrOpcode::Return) {
                if (operation.a < 0 || operation.a > 1)
                    return TypedIrScalarStackAnalysis{TypedIrScalarStackStatus::Invalid, 0};
                pops = operation.a;
                returns = true;
            } else if (opcode == TypedIrOpcode::ReturnInt) {
                pops = 1;
                returns = true;
            } else {
                return analysis;
            }
            if (depth < pops)
                return TypedIrScalarStackAnalysis{TypedIrScalarStackStatus::Invalid, 0};
            depth -= pops;
            depth += pushes;
            analysis.peakStack = std::max(analysis.peakStack, depth);
            if (depth > function.maxStack)
                return TypedIrScalarStackAnalysis{
                    TypedIrScalarStackStatus::Invalid, analysis.peakStack};
            if (returns) {
                if (!isFinalOperation || depth != 0)
                    return TypedIrScalarStackAnalysis{
                        TypedIrScalarStackStatus::Invalid, analysis.peakStack};
                hasReturn = true;
            } else if (hasReturn) {
                return TypedIrScalarStackAnalysis{
                    TypedIrScalarStackStatus::Invalid, analysis.peakStack};
            }
        }
    }
    if (!hasReturn)
        return analysis;
    analysis.status = TypedIrScalarStackStatus::Valid;
    return analysis;
}

TypedIrScalarControlFlowAnalysis analyzeTypedIrScalarControlFlow(const TypedIrFunction& function)
{
    TypedIrScalarControlFlowAnalysis analysis;
    if (function.blocks.empty() || function.maxStack < 0 || function.localCount < 0)
        return analysis;

    core::pmr::Vector<const TypedIrOperationRecord*> operations{&core::defaultCpuMemoryResource()};
    for (const auto& block : function.blocks) {
        for (const auto& operation : block.operations)
            operations.push_back(&operation);
    }
    if (operations.empty())
        return analysis;
    const auto operationCount = operations.size();
    const auto localCount = static_cast<std::size_t>(function.localCount);
    if (localCount != 0u && operationCount > std::numeric_limits<std::size_t>::max() / localCount)
        return analysis;

    core::pmr::Vector<int> depths(operationCount, -1, &core::defaultCpuMemoryResource());
    core::pmr::Vector<std::uint8_t> initialized(
        operationCount * localCount, 0u, &core::defaultCpuMemoryResource());
    core::pmr::Vector<std::size_t> worklist{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> queued(operationCount, 0u, &core::defaultCpuMemoryResource());
    depths[0] = 0;
    for (int index = 0; index < function.parameterCount && index < function.localCount; ++index)
        initialized[static_cast<std::size_t>(index)] = 1u;
    worklist.push_back(0u);
    queued[0] = 1u;
    bool hasReturn = false;

    std::size_t currentPc = 0u;
    const TypedIrOperationRecord* currentOperation = nullptr;
    core::pmr::String failureReason(
        "operation contract is invalid", &core::defaultCpuMemoryResource());
    auto invalidResult = [&]() {
        analysis.status = TypedIrScalarStackStatus::Invalid;
        analysis.failureOperationIndex = static_cast<int>(currentPc);
        if (currentOperation != nullptr) {
            analysis.failureOperation = typedIrOpcodeName(currentOperation->opcode);
            analysis.failureSource = currentOperation->source;
        }
        analysis.failureReason = failureReason;
        return analysis;
    };
    auto reasonWithValue = [](const char* prefix, int value) {
        core::pmr::String reason(prefix, &core::defaultCpuMemoryResource());
        reason += std::to_string(value);
        return reason;
    };
    auto appendPredecessors = [&operations](core::pmr::String& reason, std::size_t target) {
        reason += "; predecessors=";
        bool first = true;
        for (std::size_t candidate = 0; candidate < operations.size(); ++candidate) {
            const auto& predecessor = *operations[candidate];
            const bool jumpEdge =
                (predecessor.opcode == TypedIrOpcode::Jump
                    || predecessor.opcode == TypedIrOpcode::JumpIfFalse
                    || predecessor.opcode == TypedIrOpcode::JumpIfTrue
                    || predecessor.opcode == TypedIrOpcode::JumpIfLocalIntLtConstFalse)
                && predecessor.a == static_cast<int>(target);
            const bool terminal = predecessor.opcode == TypedIrOpcode::Jump
                || predecessor.opcode == TypedIrOpcode::Return
                || predecessor.opcode == TypedIrOpcode::ReturnInt
                || predecessor.opcode == TypedIrOpcode::Throw;
            const bool fallthroughEdge = candidate + 1u == target && !terminal;
            if (!jumpEdge && !fallthroughEdge)
                continue;
            if (!first)
                reason += ",";
            first = false;
            reason += std::to_string(candidate);
            reason += ":";
            reason += typedIrOpcodeName(predecessor.opcode);
            reason += "(a=";
            reason += std::to_string(predecessor.a);
            reason += ",b=";
            reason += std::to_string(predecessor.b);
            reason += ")";
        }
        if (first)
            reason += "none";
    };
    core::pmr::String mergeFailure{&core::defaultCpuMemoryResource()};
    auto merge = [&](std::size_t target, int depth, const std::uint8_t* locals) -> bool {
        if (target >= operationCount) {
            mergeFailure = "branch target is outside the function: ";
            mergeFailure += std::to_string(target);
            return false;
        }
        auto* targetLocals = localCount == 0u ? nullptr : initialized.data() + target * localCount;
        bool changed = false;
        if (depths[target] < 0) {
            depths[target] = depth;
            if (localCount != 0u)
                std::copy(locals, locals + localCount, targetLocals);
            changed = true;
        } else {
            if (depths[target] != depth) {
                mergeFailure = "operand stack depth mismatch at branch target ";
                mergeFailure += std::to_string(target);
                mergeFailure += ": existing=";
                mergeFailure += std::to_string(depths[target]);
                mergeFailure += ", incoming=";
                mergeFailure += std::to_string(depth);
                return false;
            }
            for (std::size_t local = 0; local < localCount; ++local) {
                const auto merged = static_cast<std::uint8_t>(targetLocals[local] & locals[local]);
                if (merged != targetLocals[local]) {
                    targetLocals[local] = merged;
                    changed = true;
                }
            }
        }
        if (changed && !queued[target]) {
            worklist.push_back(target);
            queued[target] = 1u;
        }
        return true;
    };

    while (!worklist.empty()) {
        const auto pc = worklist.back();
        worklist.pop_back();
        queued[pc] = 0u;
        const auto& operation = *operations[pc];
        currentPc = pc;
        currentOperation = &operation;
        failureReason = "operation contract is invalid";
        int depth = depths[pc];
        core::pmr::Vector<std::uint8_t> localState(
            localCount, 0u, &core::defaultCpuMemoryResource());
        if (localCount != 0u) {
            const auto* entryLocals = initialized.data() + pc * localCount;
            std::copy(entryLocals, entryLocals + localCount, localState.begin());
        }
        // The per-PC table is the immutable incoming state. Operation writes
        // belong only to outgoing edges; mutating the table here loses the
        // initial path when a loop back-edge is merged later.
        auto* locals = localCount == 0u ? nullptr : localState.data();
        int pops = 0;
        int pushes = 0;
        bool returns = false;
        bool unconditionalJump = false;
        bool conditionalJump = false;
        const auto opcode = operation.opcode;
        if (opcode == TypedIrOpcode::LoadNil || opcode == TypedIrOpcode::LoadBool
            || opcode == TypedIrOpcode::LoadInt || opcode == TypedIrOpcode::LoadInt64
            || opcode == TypedIrOpcode::LoadFloat || opcode == TypedIrOpcode::LoadString) {
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LoadLocal || opcode == TypedIrOpcode::AddrLocal) {
            if (operation.a < 0 || operation.a >= function.localCount) {
                failureReason =
                    reasonWithValue("local slot is outside the function: ", operation.a);
                return invalidResult();
            }
            if (locals[static_cast<std::size_t>(operation.a)] == 0u) {
                failureReason = reasonWithValue(
                    "local is not definitely assigned on every incoming edge: ", operation.a);
                appendPredecessors(failureReason, pc);
                return invalidResult();
            }
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LoadCapture || opcode == TypedIrOpcode::AddrCapture) {
            if (operation.a < 0 || operation.a >= function.captureCount)
                return invalidResult();
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LoadGlobal) {
            if (operation.a < 0)
                return invalidResult();
            pushes = 1;
        } else if (opcode == TypedIrOpcode::AddrGlobal) {
            if (operation.a < 0)
                return invalidResult();
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LoadModule) {
            if (operation.text.empty())
                return invalidResult();
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LoadModuleExport) {
            if (operation.text.empty())
                return invalidResult();
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::AddrIndex) {
            pops = 2;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::AddrField) {
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LoadField) {
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LoadIndex) {
            pops = 2;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::StoreIndex) {
            pops = 3;
        } else if (opcode == TypedIrOpcode::StoreField) {
            pops = 2;
        } else if (opcode == TypedIrOpcode::StoreLocal) {
            if (operation.a < 0 || operation.a >= function.localCount)
                return invalidResult();
            pops = 1;
        } else if (opcode == TypedIrOpcode::StoreCapture) {
            if (operation.a < 0 || operation.a >= function.captureCount)
                return invalidResult();
            pops = 1;
        } else if (opcode == TypedIrOpcode::StoreGlobal) {
            if (operation.a < 0)
                return invalidResult();
            pops = 1;
        } else if (opcode == TypedIrOpcode::Deref) {
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::StoreIndirect) {
            pops = 2;
        } else if (opcode == TypedIrOpcode::Pop) {
            pops = 1;
        } else if (opcode == TypedIrOpcode::BuiltinAppend) {
            pops = 2;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::BuiltinDelete) {
            pops = 2;
        } else if (opcode == TypedIrOpcode::BuiltinClear) {
            pops = 1;
        } else if (opcode == TypedIrOpcode::BuiltinLen || opcode == TypedIrOpcode::ConvertInt
            || opcode == TypedIrOpcode::ConvertInt64 || opcode == TypedIrOpcode::ConvertFloat
            || opcode == TypedIrOpcode::ConvertString || opcode == TypedIrOpcode::ConvertBool
            || opcode == TypedIrOpcode::ConvertBytes) {
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeList) {
            if (operation.b < 0)
                return invalidResult();
            pops = operation.b;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeDict) {
            if (operation.b < 0 || operation.b > std::numeric_limits<int>::max() / 2)
                return invalidResult();
            pops = operation.b * 2;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeRange) {
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeRangeFromStack) {
            if (operation.b < 1 || operation.b > 2)
                return invalidResult();
            pops = operation.b;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeInstance) {
            if (operation.b < 0)
                return invalidResult();
            pops = operation.b;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::RangeInit) {
            if (operation.a < 0 || operation.a >= function.localCount || operation.b < 0
                || operation.b >= function.localCount)
                return invalidResult();
            pops = 1;
        } else if (opcode == TypedIrOpcode::RangeNext) {
            if (operation.a < 0 || operation.a >= function.localCount || operation.b < 0
                || operation.b >= function.localCount) {
                failureReason = "range iterator or state local is outside the function";
                return invalidResult();
            }
            if (locals[static_cast<std::size_t>(operation.a)] == 0u) {
                failureReason = reasonWithValue(
                    "range iterator local is not definitely assigned: ", operation.a);
                return invalidResult();
            }
            if (locals[static_cast<std::size_t>(operation.b)] == 0u) {
                failureReason =
                    reasonWithValue("range state local is not definitely assigned: ", operation.b);
                return invalidResult();
            }
            const int keySlot = static_cast<int>(static_cast<std::int32_t>(operation.i64 >> 32));
            const int valueSlot =
                static_cast<int>(static_cast<std::int32_t>(operation.i64 & 0xffffffffLL));
            if ((keySlot >= 0 && keySlot >= function.localCount)
                || (valueSlot >= 0 && valueSlot >= function.localCount))
                return invalidResult();
            pushes = 1;
        } else if (opcode == TypedIrOpcode::Neg || opcode == TypedIrOpcode::Not) {
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::Add || opcode == TypedIrOpcode::Sub
            || opcode == TypedIrOpcode::Mul || opcode == TypedIrOpcode::Div
            || opcode == TypedIrOpcode::Mod || opcode == TypedIrOpcode::IntAdd
            || opcode == TypedIrOpcode::IntSub || opcode == TypedIrOpcode::IntMul
            || opcode == TypedIrOpcode::IntDiv || opcode == TypedIrOpcode::IntMod
            || opcode == TypedIrOpcode::Pow || opcode == TypedIrOpcode::FloorDiv
            || opcode == TypedIrOpcode::BitAnd || opcode == TypedIrOpcode::BitOr
            || opcode == TypedIrOpcode::BitXor || opcode == TypedIrOpcode::BitClear
            || opcode == TypedIrOpcode::ShiftLeft || opcode == TypedIrOpcode::ShiftRight
            || opcode == TypedIrOpcode::CmpEq || opcode == TypedIrOpcode::CmpNe
            || opcode == TypedIrOpcode::CmpLt || opcode == TypedIrOpcode::CmpLe
            || opcode == TypedIrOpcode::CmpGt || opcode == TypedIrOpcode::CmpGe
            || opcode == TypedIrOpcode::IntCmpEq || opcode == TypedIrOpcode::IntCmpNe
            || opcode == TypedIrOpcode::IntCmpLt || opcode == TypedIrOpcode::IntCmpLe
            || opcode == TypedIrOpcode::IntCmpGt || opcode == TypedIrOpcode::IntCmpGe) {
            pops = 2;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::Call || opcode == TypedIrOpcode::CallHost) {
            if (operation.b < 0)
                return invalidResult();
            pops = operation.b;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::CallValue) {
            if (operation.b < 0 || operation.b == std::numeric_limits<int>::max())
                return invalidResult();
            pops = operation.b + 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeClosure) {
            if (operation.a < 0 || operation.b < 0)
                return invalidResult();
            pops = operation.b;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeFuncValue) {
            if (operation.a < 0)
                return invalidResult();
            pushes = 1;
        } else if (opcode == TypedIrOpcode::MakeMethodValue) {
            if (operation.a < 0)
                return invalidResult();
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::LocalAddIntConst
            || opcode == TypedIrOpcode::LocalSubIntConst
            || opcode == TypedIrOpcode::LocalAddInt64Const
            || opcode == TypedIrOpcode::LocalSubInt64Const
            || opcode == TypedIrOpcode::LocalAddFloatConst
            || opcode == TypedIrOpcode::LocalSubFloatConst) {
            if (operation.a < 0 || operation.a >= function.localCount
                || locals[static_cast<std::size_t>(operation.a)] == 0u)
                return invalidResult();
        } else if (opcode == TypedIrOpcode::LocalAddLocal) {
            if (operation.a < 0 || operation.a >= function.localCount || operation.b < 0
                || operation.b >= function.localCount
                || locals[static_cast<std::size_t>(operation.a)] == 0u
                || locals[static_cast<std::size_t>(operation.b)] == 0u)
                return invalidResult();
        } else if (opcode == TypedIrOpcode::Await) {
            pops = 1;
            pushes = 1;
        } else if (opcode == TypedIrOpcode::Yield) {
            pops = 1;
        } else if (opcode == TypedIrOpcode::Jump) {
            unconditionalJump = true;
        } else if (opcode == TypedIrOpcode::PushHandler || opcode == TypedIrOpcode::PopHandler) {
            // Handler-stack effects are proven separately by the exception
            // CFG contract. They do not consume operand-stack values.
        } else if (opcode == TypedIrOpcode::JumpIfFalse || opcode == TypedIrOpcode::JumpIfTrue) {
            pops = 1;
            conditionalJump = true;
        } else if (opcode == TypedIrOpcode::JumpIfLocalIntLtConstFalse) {
            if (operation.b < 0 || operation.b >= function.localCount
                || locals[static_cast<std::size_t>(operation.b)] == 0u)
                return invalidResult();
            conditionalJump = true;
        } else if (opcode == TypedIrOpcode::Return) {
            if (operation.a < 0 || operation.a > 1)
                return invalidResult();
            pops = operation.a;
            returns = true;
        } else if (opcode == TypedIrOpcode::ReturnInt || opcode == TypedIrOpcode::Throw) {
            pops = 1;
            returns = true;
        } else {
            return analysis;
        }
        if (depth < pops) {
            failureReason = "operand stack underflow: depth=";
            failureReason += std::to_string(depth);
            failureReason += ", required=";
            failureReason += std::to_string(pops);
            return invalidResult();
        }
        depth = depth - pops + pushes;
        analysis.peakStack = std::max(analysis.peakStack, depth);
        if (depth > function.maxStack) {
            failureReason = "operand stack exceeds maxStack: depth=";
            failureReason += std::to_string(depth);
            failureReason += ", maxStack=";
            failureReason += std::to_string(function.maxStack);
            return invalidResult();
        }
        if (opcode == TypedIrOpcode::StoreLocal)
            locals[static_cast<std::size_t>(operation.a)] = 1u;
        if (opcode == TypedIrOpcode::RangeInit) {
            locals[static_cast<std::size_t>(operation.a)] = 1u;
            locals[static_cast<std::size_t>(operation.b)] = 1u;
        }
        if (opcode == TypedIrOpcode::RangeNext) {
            const int keySlot = static_cast<int>(static_cast<std::int32_t>(operation.i64 >> 32));
            const int valueSlot =
                static_cast<int>(static_cast<std::int32_t>(operation.i64 & 0xffffffffLL));
            if (keySlot >= 0)
                locals[static_cast<std::size_t>(keySlot)] = 1u;
            if (valueSlot >= 0)
                locals[static_cast<std::size_t>(valueSlot)] = 1u;
        }
        if (returns) {
            if (depth != 0) {
                failureReason =
                    reasonWithValue("terminal operation leaves values on the stack: ", depth);
                return invalidResult();
            }
            hasReturn = true;
            continue;
        }
        if (unconditionalJump || conditionalJump) {
            if (operation.a < 0 || operation.a >= static_cast<int>(operationCount)) {
                failureReason =
                    reasonWithValue("branch target is outside the function: ", operation.a);
                return invalidResult();
            }
            if (!merge(static_cast<std::size_t>(operation.a), depth, locals)) {
                failureReason = mergeFailure;
                return invalidResult();
            }
        }
        if ((opcode == TypedIrOpcode::Yield || opcode == TypedIrOpcode::Await)
            && pc + 1u >= operationCount)
            continue;
        if (!unconditionalJump) {
            if (pc + 1u >= operationCount) {
                failureReason = "reachable control flow falls off the function";
                return invalidResult();
            }
            if (!merge(pc + 1u, depth, locals)) {
                failureReason = mergeFailure;
                return invalidResult();
            }
        }
    }
    if (!hasReturn)
        return analysis;
    analysis.status = TypedIrScalarStackStatus::Valid;
    return analysis;
}

} // namespace arp::scriptvm

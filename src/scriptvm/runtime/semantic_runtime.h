#pragma once

// ArpScript: backend-neutral semantic operations.

#include "core/borrowed_lookup_cache.h"
#include "core/cpu_allocator.h"
#include "core/int32_divisor.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/execution_slot.h"
#include "scriptvm/runtime/scalar_value.h"
#include "scriptvm/runtime/type_contract.h"
#include "scriptvm/runtime/value.h"

#include <cmath>
#include <limits>

namespace arp::scriptvm {

struct PrivateDictionaryWriteTarget {
    Value* destination = nullptr;
    std::uint32_t operand = 0;
    int key = 0;
};

using PrivateDictionaryWriteScratch = core::pmr::Vector<PrivateDictionaryWriteTarget>;

class ModuleRegistry;
class ExecutionSlot;
class ExecutionSlotBuffer;

enum class SemanticBinaryOperation : std::uint8_t {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    BitClear,
    ShiftLeft,
    ShiftRight,
    Pow,
    FloorDiv,
};

enum class SemanticCompareOperation : std::uint8_t {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

enum class SemanticConversionOperation : std::uint8_t {
    Int,
    Int64,
    Float,
    String,
    Bool,
    Bytes,
};

struct SemanticOperationResult {
    // Use the shared undefined carrier so constructing a result does not
    // allocate a reflection variant on every semantic operation.
    Value value = Value::undefined();
    ScriptDiagnostic diagnostic;

    bool ok() const { return diagnostic.code == ScriptDiagnosticCode::None; }
};

struct SemanticRangeNextResult {
    Value key = Value::undefined();
    Value value = Value::undefined();
    ScriptDiagnostic diagnostic;
    bool hasMore = false;

    bool ok() const { return diagnostic.code == ScriptDiagnosticCode::None; }
};

struct SemanticSlotOperationResult {
    ExecutionSlot value;
    ScriptDiagnostic diagnostic;

    bool ok() const { return diagnostic.code == ScriptDiagnosticCode::None; }
};

struct SemanticSlotRangeNextResult {
    ExecutionSlot key;
    ExecutionSlot value;
    ScriptDiagnostic diagnostic;
    bool hasMore = false;

    bool ok() const { return diagnostic.code == ScriptDiagnosticCode::None; }
};

// The interpreter needs a status and actual carriers on successful iteration.
// It publishes the existing VM diagnostic only when the step fails, avoiding
// construction of source/message strings for each element and end-of-range.
struct SemanticCollectionStep {
    ExecutionSlot key;
    ExecutionSlot value;
    ScriptDiagnosticCode code = ScriptDiagnosticCode::None;
    bool hasMore = false;

    bool ok() const { return code == ScriptDiagnosticCode::None; }
};

class ARP_API ScriptSemanticRuntime final {
public:
    // A 32-bit payload is known after the slot's ABI/type check. Specialize
    // the operation at the call site; no numeric-kind or operation dispatch.
    // The wide intermediate proves add/sub/mul/div/mod cannot overflow int64.
    template <SemanticBinaryOperation Operation>
    static ScalarOperationResult intBinary(int lhs, int rhs)
    {
        static_assert(std::numeric_limits<int>::digits <= 31);
        static_assert(Operation == SemanticBinaryOperation::Add
            || Operation == SemanticBinaryOperation::Sub
            || Operation == SemanticBinaryOperation::Mul
            || Operation == SemanticBinaryOperation::Div
            || Operation == SemanticBinaryOperation::Mod
            || Operation == SemanticBinaryOperation::BitAnd
            || Operation == SemanticBinaryOperation::BitOr
            || Operation == SemanticBinaryOperation::BitXor
            || Operation == SemanticBinaryOperation::BitClear);
        ScalarOperationResult result;
        const std::int64_t a = lhs, b = rhs;
        std::int64_t value;
        if constexpr (Operation == SemanticBinaryOperation::Div
            || Operation == SemanticBinaryOperation::Mod) {
            if (b == 0) {
                result.code = ScriptDiagnosticCode::DivisionByZero;
                result.message = Operation == SemanticBinaryOperation::Div ? "division by zero"
                                                                           : "modulo by zero";
                return result;
            }
        }
        if constexpr (Operation == SemanticBinaryOperation::Add)
            value = a + b;
        else if constexpr (Operation == SemanticBinaryOperation::Sub)
            value = a - b;
        else if constexpr (Operation == SemanticBinaryOperation::Mul)
            value = a * b;
        else if constexpr (Operation == SemanticBinaryOperation::Div)
            value = a / b;
        else if constexpr (Operation == SemanticBinaryOperation::Mod)
            value = a % b;
        else if constexpr (Operation == SemanticBinaryOperation::BitAnd)
            value = a & b;
        else if constexpr (Operation == SemanticBinaryOperation::BitOr)
            value = a | b;
        else if constexpr (Operation == SemanticBinaryOperation::BitXor)
            value = a ^ b;
        else
            value = a & ~b;
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
            result.code = ScriptDiagnosticCode::IntegerOverflow;
            result.message = "integer arithmetic overflow";
        } else
            result.value = ScriptScalar(static_cast<int>(value));
        return result;
    }

    template <SemanticBinaryOperation Operation>
    static ScalarOperationResult intBinary(int lhs, const core::Int32Divisor& divisor)
    {
        static_assert(
            Operation == SemanticBinaryOperation::Div || Operation == SemanticBinaryOperation::Mod);
        static_assert(std::numeric_limits<int>::digits <= 31);
        if (!divisor.valid())
            return intBinary<Operation>(lhs, divisor.divisor());
        ScalarOperationResult result;
        if constexpr (Operation == SemanticBinaryOperation::Mod) {
            result.value = ScriptScalar(static_cast<int>(divisor.remainder(lhs)));
        } else {
            const auto value = divisor.quotient(lhs);
            if (value < std::numeric_limits<int>::min()
                || value > std::numeric_limits<int>::max()) {
                result.code = ScriptDiagnosticCode::IntegerOverflow;
                result.message = "integer arithmetic overflow";
            } else
                result.value = ScriptScalar(static_cast<int>(value));
        }
        return result;
    }

    template <SemanticBinaryOperation Operation>
    static ScalarOperationResult floatBinary(double lhs, double rhs)
    {
        static_assert(Operation == SemanticBinaryOperation::Add
            || Operation == SemanticBinaryOperation::Sub
            || Operation == SemanticBinaryOperation::Mul
            || Operation == SemanticBinaryOperation::Div
            || Operation == SemanticBinaryOperation::Mod
            || Operation == SemanticBinaryOperation::Pow
            || Operation == SemanticBinaryOperation::FloorDiv);
        ScalarOperationResult result;
        if constexpr (Operation == SemanticBinaryOperation::Div
            || Operation == SemanticBinaryOperation::Mod
            || Operation == SemanticBinaryOperation::FloorDiv) {
            if (rhs == 0.0) {
                result.code = ScriptDiagnosticCode::DivisionByZero;
                result.message = "division or modulo by zero";
                return result;
            }
        }
        double value;
        if constexpr (Operation == SemanticBinaryOperation::Add)
            value = lhs + rhs;
        else if constexpr (Operation == SemanticBinaryOperation::Sub)
            value = lhs - rhs;
        else if constexpr (Operation == SemanticBinaryOperation::Mul)
            value = lhs * rhs;
        else if constexpr (Operation == SemanticBinaryOperation::Div)
            value = lhs / rhs;
        else if constexpr (Operation == SemanticBinaryOperation::Mod)
            value = std::fmod(lhs, rhs);
        else if constexpr (Operation == SemanticBinaryOperation::Pow)
            value = std::pow(lhs, rhs);
        else
            value = std::floor(lhs / rhs);
        result.value = ScriptScalar(value);
        return result;
    }

    // Compile-time operation, runtime payload checks. Wide integers retain
    // the shared checked implementation; no type proof is inferred from opcodes.
    template <SemanticBinaryOperation Operation>
    static bool scalarBinary(
        const ScriptScalar& lhs, const ScriptScalar& rhs, ScalarOperationResult& result)
    {
        static_assert(Operation <= SemanticBinaryOperation::Mod);
        if (!lhs.numeric() || !rhs.numeric()) {
            result = {};
            return false;
        }
        if (lhs.kind() == ScriptScalar::Kind::Float || rhs.kind() == ScriptScalar::Kind::Float) {
            result = floatBinary<Operation>(lhs.floating(), rhs.floating());
            return true;
        }
        if (lhs.kind() == ScriptScalar::Kind::Int && rhs.kind() == ScriptScalar::Kind::Int) {
            // Keep the generic scalar diagnostic text at this ABI boundary.
            if constexpr (Operation == SemanticBinaryOperation::Div
                || Operation == SemanticBinaryOperation::Mod) {
                if (rhs.integer() == 0) {
                    result = {};
                    result.code = ScriptDiagnosticCode::DivisionByZero;
                    result.message = "division or modulo by zero";
                    return true;
                }
            }
            result = intBinary<Operation>(
                static_cast<int>(lhs.integer()), static_cast<int>(rhs.integer()));
            return true;
        }
        return scalarBinary(Operation, lhs, rhs, result);
    }

    template <SemanticCompareOperation Operation, class T>
    static bool comparePayload(const T& lhs, const T& rhs)
    {
        static_assert(Operation <= SemanticCompareOperation::GreaterEqual);
        if constexpr (Operation == SemanticCompareOperation::Equal)
            return lhs == rhs;
        else if constexpr (Operation == SemanticCompareOperation::NotEqual)
            return lhs != rhs;
        else if constexpr (Operation == SemanticCompareOperation::Less)
            return lhs < rhs;
        else if constexpr (Operation == SemanticCompareOperation::LessEqual)
            return lhs <= rhs;
        else if constexpr (Operation == SemanticCompareOperation::Greater)
            return lhs > rhs;
        else
            return lhs >= rhs;
    }

    template <SemanticCompareOperation Operation>
    static bool scalarCompare(const ScriptScalar& lhs, const ScriptScalar& rhs, bool& result)
    {
        if (lhs.kind() == ScriptScalar::Kind::Nil || rhs.kind() == ScriptScalar::Kind::Nil) {
            if constexpr (Operation == SemanticCompareOperation::Equal)
                result = lhs.kind() == rhs.kind();
            else if constexpr (Operation == SemanticCompareOperation::NotEqual)
                result = lhs.kind() != rhs.kind();
            else
                result = false;
            return true;
        }
        if (lhs.numeric() && rhs.numeric()) {
            result = lhs.integral() && rhs.integral()
                ? comparePayload<Operation>(lhs.integer(), rhs.integer())
                : comparePayload<Operation>(lhs.floating(), rhs.floating());
            return true;
        }
        if (lhs.kind() == ScriptScalar::Kind::Bool && rhs.kind() == ScriptScalar::Kind::Bool) {
            result = comparePayload<Operation>(lhs.boolean(), rhs.boolean());
            return true;
        }
        return false;
    }

    // false means operands require the existing dynamic/reference semantic path.
    // true also covers checked failures; callers must inspect result.code.
    static bool scalarBinary(SemanticBinaryOperation operation,
        const ScriptScalar& lhs,
        const ScriptScalar& rhs,
        ScalarOperationResult& result);
    static bool scalarCompare(SemanticCompareOperation operation,
        const ScriptScalar& lhs,
        const ScriptScalar& rhs,
        bool& result);
    static bool compareStrings(
        SemanticCompareOperation operation, std::string_view lhs, std::string_view rhs);
    static SemanticOperationResult concatenateStrings(std::string_view lhs, std::string_view rhs);

    // Borrowed access for a proven uninterrupted nonescaping local. The caller
    // must prevent carrier replacement, aliases, host calls and suspension until
    // the access expires. No ownership or allocation is introduced by the borrow.
    class UniqueStringAccess final {
    public:
        static UniqueStringAccess borrow(ExecutionSlot& local);

        explicit operator bool() const noexcept { return text_ != nullptr; }

        bool append(std::string_view suffix) const;
        // Both sources must be independent of the destination payload.
        bool assign(std::string_view prefix, std::string_view suffix) const;

    private:
        explicit UniqueStringAccess(core::pmr::String* text) : text_(text) {}

        core::pmr::String* text_;
    };

    // Requires a caller proof that this local does not escape. Shared carriers,
    // imported/native references and borrowed literals do not mutate here.
    static bool appendUniqueString(ExecutionSlot& local, std::string_view suffix);
    // Both sources must be independent of the destination payload.
    static bool assignUniqueString(
        ExecutionSlot& local, std::string_view prefix, std::string_view suffix);
    // Requires a nonescape proof; elements retain the normal Value ABI.
    static bool reuseUniqueList(
        ExecutionSlot& local, ExecutionSlotBuffer& operands, int count, std::size_t limit);
    static bool reuseUniqueDictionary(ExecutionSlot& local,
        ExecutionSlotBuffer& operands,
        int pairCount,
        std::size_t limit,
        PrivateDictionaryWriteScratch& scratch);

    // Only an immutable, call-free construction/read/scalar-tail self edge may
    // retain this topology. Existing nodes remain real Values; commits consume
    // the original operands in the original order. Destroy before PC replay.
    class PrivateConstructionBorrow final {
    public:
        PrivateConstructionBorrow() = default;
        PrivateConstructionBorrow(const PrivateConstructionBorrow&) = delete;
        PrivateConstructionBorrow& operator=(const PrivateConstructionBorrow&) = delete;
        ~PrivateConstructionBorrow();
        bool bind(ExecutionSlot& local,
            ExecutionSlotBuffer& operands,
            int count,
            std::size_t limit,
            bool dictionary,
            PrivateDictionaryWriteScratch& scratch);
        bool commit(ExecutionSlotBuffer& operands) const;

    private:
        arp::reflection::hlist* list_ = nullptr;
        PrivateDictionaryWriteScratch* targets_ = nullptr;
        std::size_t start_ = 0;
        int count_ = 0;
        IntegerValueCacheBorrow integers_;
    };

    static ScalarOperationResult lengthScalar(const ExecutionSlot& value);
    // Checked scalar helpers used by proven-safe backend fast paths.
    static bool checkedAddInt64(std::int64_t lhs, std::int64_t rhs, std::int64_t& result);
    static bool checkedSubtractInt64(std::int64_t lhs, std::int64_t rhs, std::int64_t& result);
    static SemanticOperationResult binary(SemanticBinaryOperation operation, Value lhs, Value rhs);
    static SemanticOperationResult compare(
        SemanticCompareOperation operation, Value lhs, Value rhs);
    static SemanticOperationResult negate(Value value);
    static SemanticOperationResult logicalNot(Value value);
    static SemanticOperationResult convert(SemanticConversionOperation operation, Value value);
    // Normalize one public function argument according to the shared
    // RuntimeType contract. This is used by both VM and AOT entry paths.
    static SemanticOperationResult normalizeArgument(
        Value argument, RuntimeType expected, bool nullable, const core::pmr::String& className);
    static SemanticOperationResult length(Value value);
    static SemanticOperationResult append(Value list, Value value, std::size_t maxContainerValues);
    static SemanticOperationResult erase(Value dictionary, Value key);
    static SemanticOperationResult clear(Value container);
    static SemanticOperationResult makeList(
        core::pmr::Vector<Value>& operands, int count, std::size_t maxContainerValues);
    static SemanticOperationResult makeDict(
        core::pmr::Vector<Value>& operands, int pairCount, std::size_t maxContainerValues);
    static SemanticOperationResult makeList(
        ExecutionSlotBuffer& operands, int count, std::size_t maxContainerValues);
    static SemanticOperationResult makeDict(
        ExecutionSlotBuffer& operands, int pairCount, std::size_t maxContainerValues);
    static SemanticOperationResult makeRange(int start, int end, std::size_t maxContainerValues);
    // Consumes range bounds in bytecode order: end first, then start for the
    // two-bound form. The consumption order is part of the VM error contract.
    static SemanticOperationResult makeRangeFromStack(
        core::pmr::Vector<Value>& operands, int boundCount, std::size_t maxContainerValues);
    static SemanticOperationResult makeReflectedInstance(core::pmr::Vector<Value>& operands,
        const core::pmr::String& className,
        int argumentCount,
        std::size_t maxCallArguments);
    // Allocates the reflection-backed carrier used by a script-declared class
    // and initializes its declared slots to nil. Script constructor execution
    // remains owned by the caller because it may establish a resumable frame.
    static SemanticOperationResult makeScriptInstance(
        const core::pmr::String& className, const core::pmr::Vector<core::pmr::String>& fields);
    static SemanticRangeNextResult rangeNextCollection(Value source, int index);
    // Same collection semantics, with generated indices/bytes kept inline.
    // List/dictionary elements retain their exact mutable Value carriers.
    static SemanticSlotRangeNextResult rangeNextCollectionSlots(const Value& source, int index);
    static SemanticCollectionStep rangeNextCollectionStep(const Value& source, int index);
    static SemanticOperationResult loadIndex(Value base, Value index);
    // Index integers and byte results stay inline; elements keep Value identity.
    static SemanticSlotOperationResult loadIndexSlots(
        const Value& base, const ExecutionSlot& index);
    // Borrow one existing destination only for an uninterrupted exact indexed
    // update span. A failed bind is pure; commit preserves the public Value ABI.
    // The VM retains instruction replay for quotas, short budgets and failures.
    // The caller proves an uninterrupted span without calls, suspension,
    // container replacement, key/size changes or writes except to existing
    // integer elements. Destroy this borrow before replaying any instruction.
    class IntegerIndexUpdate;

    class IntegerIndexBorrow final {
    public:
        bool bind(const ExecutionSlot& base);
        const Value* read(const ExecutionSlot& index) const;
        bool readInteger(const ExecutionSlot& index, int& value) const;
        Value* destination(const ExecutionSlot& index) const;

    private:
        friend class IntegerIndexUpdate;
        bool resolve(const ExecutionSlot& index, const Value*& source, Value*& destination) const;
        Value* find(const Value& key, int payload) const;
        arp::reflection::hlist* list_ = nullptr;
        arp::reflection::hdict* dictionary_ = nullptr;
        // Same explicit stack-storage policy as readonly scalar index access.
        static constexpr std::size_t lookupStorageBytes = 64u;
        static constexpr std::size_t lookupEntries =
            lookupStorageBytes / core::BorrowedLookupCache<int, Value, 0u>::entryBytes;
        mutable core::BorrowedLookupCache<int, Value, lookupEntries> cache_;
        IntegerValueCacheBorrow integers_;
    };

    class IntegerIndexUpdate final {
    public:
        bool bind(const ExecutionSlot& base, const ExecutionSlot& index, int& source);
        bool bind(const IntegerIndexBorrow& borrow, const ExecutionSlot& index, int& source);
        bool readSource(int& value) const;
        void commit(int value) const;

    private:
        Value* destination_ = nullptr;
        const Value* source_ = nullptr;
    };

    static SemanticOperationResult storeIndex(Value base, Value index, Value value);
    static SemanticOperationResult loadField(Value base, const core::pmr::String& fieldName);
    // Successful missing lookup returns undefined so the caller can consult
    // script method metadata without allocating a discarded diagnostic.
    static SemanticOperationResult findField(Value base, const core::pmr::String& fieldName);
    static SemanticOperationResult loadModuleExport(
        Value module, const core::pmr::String& exportName);
    // Materialize one module through the embedding registry and normalize
    // resolver failures to the same HostError contract used by Interpreter
    // and both AOT executors. std::bad_alloc remains a quota boundary.
    static SemanticOperationResult loadModule(
        ModuleRegistry& registry, const core::pmr::String& moduleName);
    static SemanticOperationResult storeField(
        Value base, const core::pmr::String& fieldName, Value value);

    static SemanticOperationResult dereference(const ScriptPointer& pointer);
    static SemanticOperationResult storePointer(ScriptPointer& pointer, Value value);
};

} // namespace arp::scriptvm

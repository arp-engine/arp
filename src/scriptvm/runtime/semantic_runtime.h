#pragma once

// ArpScript: backend-neutral semantic operations.

#include "core/container/borrowed_lookup_cache.h"
#include "core/math/int32_divisor.h"
#include "core/memory/cpu_allocator.h"
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
    class ARP_API UniqueStringAccess final {
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
    static bool reuseUniqueList(
        ExecutionSlot& local, ExecutionSlotStackView& operands, int count, std::size_t limit);
    static bool reuseUniqueDictionary(ExecutionSlot& local,
        ExecutionSlotBuffer& operands,
        int pairCount,
        std::size_t limit,
        PrivateDictionaryWriteScratch& scratch);
    static bool reuseUniqueDictionary(ExecutionSlot& local,
        ExecutionSlotStackView& operands,
        int pairCount,
        std::size_t limit,
        PrivateDictionaryWriteScratch& scratch);

    // Only an immutable, call-free construction/read/scalar-tail self edge may
    // retain this topology. Existing nodes remain real Values; commits consume
    // the original operands in the original order. Destroy before PC replay.
    class ARP_API PrivateConstructionBorrow final {
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
        bool bind(ExecutionSlot& local,
            ExecutionSlotStackView& operands,
            int count,
            std::size_t limit,
            bool dictionary,
            PrivateDictionaryWriteScratch& scratch);
        bool commit(ExecutionSlotStackView& operands) const;
        void reset();

    private:
        template <class Operands>
        bool bindOperands(ExecutionSlot& local,
            Operands& operands,
            int count,
            std::size_t limit,
            bool dictionary,
            PrivateDictionaryWriteScratch& scratch);
        template <class Operands> bool commitOperands(Operands& operands) const;
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
    static SemanticOperationResult makeList(
        ExecutionSlotStackView& operands, int count, std::size_t maxContainerValues);
    static SemanticOperationResult makeDict(
        ExecutionSlotStackView& operands, int pairCount, std::size_t maxContainerValues);
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
    class ARP_API IntegerIndexUpdate;

    class ARP_API IntegerIndexBorrow final {
    public:
        bool bind(const ExecutionSlot& base);

        const Value* readIntegerCarrier(int index) const
        {
            if (list_)
                return index >= 0 && static_cast<std::size_t>(index) < list_->size()
                    ? &(*list_)[static_cast<std::size_t>(index)]
                    : nullptr;
            if (!dictionary_)
                return nullptr;
            const auto* key = integers_.find(index);
            const auto* payload = key ? exactValuePayload<int>(*key) : nullptr;
            return payload ? find(*key, *payload) : nullptr;
        }

        const Value* read(const ExecutionSlot& index) const
        {
            const auto* raw = index.scalarPointer<int>();
            if (!raw || (!index.inlineScalar() && index.boxed().is_native_reference()))
                return nullptr;
            if (list_)
                return *raw >= 0 && static_cast<std::size_t>(*raw) < list_->size()
                    ? &(*list_)[static_cast<std::size_t>(*raw)]
                    : nullptr;
            if (!dictionary_)
                return nullptr;
            const auto* key = index.inlineScalar() ? integers_.find(*raw) : &index.boxed();
            const auto* payload = key ? checkedOwnedValuePayload<int>(*key) : nullptr;
            return payload ? find(*key, *payload) : nullptr;
        }

        bool readInteger(const ExecutionSlot& index, int& value) const
        {
            const auto* element = read(index);
            const auto* payload = element ? checkedOwnedValuePayload<int>(*element) : nullptr;
            if (!payload)
                return false;
            value = *payload;
            return true;
        }

        Value* destination(const ExecutionSlot& index) const;

    private:
        friend class IntegerIndexUpdate;
        bool resolve(const ExecutionSlot& index, const Value*& source, Value*& destination) const;

        bool resolveInteger(int index, const Value*& source, Value*& destination) const
        {
            const auto* key = integers_.find(index);
            const auto* actual = key ? exactValuePayload<int>(*key) : nullptr;
            if (!actual)
                return false;
            if (list_) {
                if (index < 0 || static_cast<std::size_t>(index) >= list_->size() || *actual < 0
                    || static_cast<std::size_t>(*actual) >= list_->size())
                    return false;
                source = &(*list_)[static_cast<std::size_t>(index)];
                destination = &(*list_)[static_cast<std::size_t>(*actual)];
            } else if (dictionary_)
                source = destination = find(*key, *actual);
            else
                return false;
            return source != nullptr && destination != nullptr;
        }

        Value* find(const Value& key, int payload) const
        {
            const Value* element;
            if (cache_.find(payload, element))
                return const_cast<Value*>(element);
            return findUncached(key, payload);
        }

        Value* findUncached(const Value& key, int payload) const;
        arp::reflection::hlist* list_ = nullptr;
        arp::reflection::hdict* dictionary_ = nullptr;
        // Same explicit stack-storage policy as readonly scalar index access.
        static constexpr std::size_t lookupStorageBytes = 64u;
        static constexpr std::size_t lookupEntries =
            lookupStorageBytes / core::BorrowedLookupCache<int, Value, 0u>::entryBytes;
        mutable core::BorrowedLookupCache<int, Value, lookupEntries> cache_;
        IntegerValueCacheBorrow integers_;
    };

    class ARP_API IntegerIndexUpdate final {
    public:
        bool bind(const ExecutionSlot& base, const ExecutionSlot& index, int& source);
        bool bind(const IntegerIndexBorrow& borrow, const ExecutionSlot& index, int& source);

        // The caller retains the original inline-index semantics. In particular,
        // list reads use index while writes use makeInt(index)'s actual payload.
        bool bindInteger(const IntegerIndexBorrow& borrow, int index, int& source)
        {
            destination_ = nullptr;
            source_ = nullptr;
            integers_ = borrow.integers_;
            const Value* element;
            Value* destination;
            return borrow.resolveInteger(index, element, destination)
                && bindResolved(element, destination, source);
        }

        bool readSource(int& value) const;

        void commit(int value) const
        {
            assert(destination_ != nullptr);
            // Replace the carrier, preserving aliases and the cache's actual
            // payload. A cold cache miss retains makeInt's allocation/quota path.
            if (const auto* cached = integers_.find(value))
                *destination_ = *cached;
            else
                *destination_ = makeInt(value);
        }

    private:
        bool bindResolved(const Value* element, Value* destination, int& source)
        {
            const auto* actual = exactValuePayload<int>(*element);
            if (!actual || (destination != element && !exactValuePayload<int>(*destination)))
                return false;
            source = *actual;
            destination_ = destination;
            source_ = element;
            return true;
        }

        Value* destination_ = nullptr;
        const Value* source_ = nullptr;
        IntegerValueCacheBorrow integers_;
    };

    // A callback-free readonly relation. Owners retain container topology and
    // dictionary key bytes until rebinding; mapped values are read on every use.
    class ARP_API ReadonlyIndexBorrow final {
    public:
        bool bind(const ExecutionSlot& base);

        const Value* readIntegerCarrier(int index) const
        {
            return !strings_ ? integers_.readIntegerCarrier(index) : nullptr;
        }

        const Value* read(const ExecutionSlot& index) const
        {
            if (!strings_)
                return integers_.read(index);
            const auto* carrier = index.boxedCarrier();
            const auto* text = index.immutableLiteral();
            if (!text && carrier)
                text = exactValuePayload<core::pmr::String>(*carrier);
            // An exact selector binds the same type_of<String> as the admitted keys.
            // No host callback or key/metadata mutation is allowed during this borrow.
            assert(!text || !carrier
                || (carrier->class_() && carrier->class_()->hasBuiltinOrdering()));
            if (!text)
                return nullptr;
            const Value* element;
            if (cache_.find({text}, element))
                return element;
            return cacheString(index.boxed());
        }

        // Nested indexing already has the owner's actual key carrier. Borrow
        // it directly instead of manufacturing a temporary shared slot.
        const Value* readCarrier(const Value& key) const
        {
            const auto* text = exactValuePayload<core::pmr::String>(key);
            if (!strings_ || !text)
                return nullptr;
            const Value* element;
            return cache_.find({text}, element) ? element : cacheString(key);
        }

    private:
        const Value* cacheString(const Value& key) const
        {
            const auto found = strings_->find(key);
            if (found == strings_->end())
                return nullptr;
            const auto* element = &found->second;
            // Retain the owner's key object, never a temporary lookup string.
            cache_.insert({exactValuePayload<core::pmr::String>(found->first)}, element);
            return element;
        }

    public:
        bool prepareUpdate(
            const ExecutionSlot& index, IntegerIndexUpdate& update, int& source) const
        {
            return !strings_ && update.bind(integers_, index, source);
        }

        bool prepareIntegerUpdate(int index, IntegerIndexUpdate& update, int& source) const
        {
            return !strings_ && update.bindInteger(integers_, index, source);
        }

        bool readInteger(const ExecutionSlot& index, int& value) const
        {
            if (!strings_)
                return integers_.readInteger(index, value);
            const auto* element = read(index);
            const auto* payload = element ? checkedOwnedValuePayload<int>(*element) : nullptr;
            if (!payload)
                return false;
            value = *payload;
            return true;
        }

    private:
        struct StringKey {
            const core::pmr::String* text;

            bool operator==(const StringKey& other) const { return *text == *other.text; }
        };

        IntegerIndexBorrow integers_;
        const arp::reflection::hdict* strings_ = nullptr;
        static constexpr std::size_t lookupStorageBytes = 64u;
        static constexpr std::size_t lookupEntries =
            lookupStorageBytes / core::BorrowedLookupCache<StringKey, Value, 0u>::entryBytes;
        mutable core::BorrowedLookupCache<StringKey, Value, lookupEntries> cache_;
    };

    // Both owners and selector nodes must remain alive and unchanged throughout
    // this scope. Mapped Values may be replaced: cache only their real addresses.
    // Generated closed regions destroy this relation before canonical replay.
    class StableStringIndexBorrow final {
    public:
        explicit StableStringIndexBorrow(const ReadonlyIndexBorrow& owner) : owner_(owner) {}

        const Value* read(const Value& key) const
        {
            const Value* element;
            if (cache_.find(&key, element))
                return element;
            element = owner_.readCarrier(key);
            if (element)
                cache_.insert(&key, element);
            return element;
        }

    private:
        static constexpr std::size_t lookupStorageBytes = 64u;
        static constexpr std::size_t lookupEntries =
            lookupStorageBytes / core::BorrowedLookupCache<const Value*, Value, 0u>::entryBytes;
        const ReadonlyIndexBorrow& owner_;
        mutable core::BorrowedLookupCache<const Value*, Value, lookupEntries> cache_;
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

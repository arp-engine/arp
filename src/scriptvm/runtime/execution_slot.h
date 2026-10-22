#pragma once

#include "scriptvm/runtime/scalar_value.h"

#include <algorithm>
#include <cassert>
#include <memory>

namespace arp::scriptvm {

// Interpreter-produced immutable scalars remain inline. Imported Value objects
// retain their exact reference carrier (including mutable host scalar aliases).
// Capture/host/container/continuation edges explicitly materialize a Value.
class ExecutionSlot final {
    using Literal = const core::pmr::String*;
    enum class Kind : std::uint8_t { Scalar, Boxed, Literal, Method };

    // Payloads overlap: inline scalars must not pay for a second reference
    // payload. Lifetime transitions use the standard construct/destroy APIs.
    union Storage {
        ScriptScalar scalar;
        Value value;
        Literal literal;
        ScriptMethod method;

        Storage() : scalar() {}

        explicit Storage(ScriptScalar v) : scalar(v) {}

        ~Storage() {}
    };

public:
    using ReflectionValueCarrier = void;
    ExecutionSlot() = default;

    ExecutionSlot(const Value& value) { setValue(value); }

    ExecutionSlot(Value&& value) { setValue(std::move(value)); }

    explicit ExecutionSlot(ScriptScalar scalar) : storage_(scalar) {}

    explicit ExecutionSlot(ScriptMethod method)
    {
        std::uninitialized_move_n(&method, 1, &storage_.method);
        kind_ = Kind::Method;
    }

    ExecutionSlot(const ExecutionSlot& other)
    {
        if (other.kind_ == Kind::Scalar)
            storage_.scalar = other.storage_.scalar;
        else
            setValue(other.boxed());
    }

    ExecutionSlot(ExecutionSlot&& other) noexcept { moveFrom(other); }

    ~ExecutionSlot() { destroy(); }

    ExecutionSlot& operator=(const ExecutionSlot& other)
    {
        if (this != &other) {
            if (other.kind_ == Kind::Scalar)
                setScalar(other.storage_.scalar);
            else
                setValue(other.boxed());
        }
        return *this;
    }

    ExecutionSlot& operator=(ExecutionSlot&& other) noexcept
    {
        if (this != &other)
            moveFrom(other);
        return *this;
    }

    ExecutionSlot& operator=(const Value& value)
    {
        setValue(value);
        return *this;
    }

    ExecutionSlot& operator=(Value&& value)
    {
        setValue(std::move(value));
        return *this;
    }

    ExecutionSlot& operator=(ScriptScalar scalar) noexcept
    {
        setScalar(scalar);
        return *this;
    }

    // The caller must retain an immutable source until consumption/materialization.
    // VM uses this only for its privately owned validated Program snapshot.
    static ExecutionSlot borrowLiteral(const core::pmr::String& text)
    {
        ExecutionSlot slot;
        slot.storage_.literal = &text;
        slot.kind_ = Kind::Literal;
        return slot;
    }

    static ExecutionSlot borrowLiteral(core::pmr::String&&) = delete;
    static ExecutionSlot borrowLiteral(const core::pmr::String&&) = delete;

    const core::pmr::String* immutableLiteral() const noexcept
    {
        return kind_ == Kind::Literal ? storage_.literal : nullptr;
    }

    bool stringView(std::string_view& text) const
    {
        if (kind_ == Kind::Literal) {
            text = *storage_.literal;
            return true;
        }
        if (kind_ == Kind::Boxed) {
            if (const auto* actual = checkedValuePayload<core::pmr::String>(storage_.value)) {
                text = *actual;
                return true;
            }
        }
        return false;
    }

    const ScriptMethod* method() const
    {
        if (kind_ == Kind::Method)
            return &storage_.method;
        if (kind_ == Kind::Boxed)
            return checkedValuePayload<ScriptMethod>(storage_.value);
        return nullptr;
    }

    const ScriptClosure* closure() const
    {
        return kind_ == Kind::Boxed ? checkedValuePayload<ScriptClosure>(storage_.value) : nullptr;
    }

    // Actual owned C++ payloads are mutually exclusive. A positive selector
    // avoids querying the other callable's reflection type on every call.
    // Native/cv/module selector misses retain the original ordered access.
    void scriptCallable(const ScriptMethod*& methodValue, const ScriptClosure*& closureValue) const
    {
        methodValue = nullptr;
        closureValue = nullptr;
        if (kind_ == Kind::Method) {
            methodValue = &storage_.method;
            return;
        }
        if (kind_ != Kind::Boxed)
            return;
        if (const auto* actual = exactValuePayload<ScriptMethod>(storage_.value)) {
            methodValue = actual;
            assert(method() == methodValue && closure() == nullptr);
            return;
        }
        if (const auto* actual = exactValuePayload<ScriptClosure>(storage_.value)) {
            closureValue = actual;
            assert(method() == nullptr && closure() == closureValue);
            return;
        }
        methodValue = method();
        closureValue = closure();
    }

    bool scalar(ScriptScalar& value) const
    {
        if (kind_ == Kind::Scalar) {
            value = storage_.scalar;
            return true;
        }
        return kind_ == Kind::Boxed && ScriptScalar::read(storage_.value, value);
    }

    template <class T> bool is() const
    {
        if (kind_ == Kind::Scalar) {
            if constexpr (std::is_void_v<T>)
                return storage_.scalar.kind() == ScriptScalar::Kind::Nil;
            else
                return storage_.scalar.ptr<T>() != nullptr;
        }
        if (kind_ == Kind::Boxed)
            return storage_.value.is<T>();
        if (kind_ == Kind::Literal)
            return std::is_same_v<T, core::pmr::String>;
        return std::is_same_v<T, ScriptMethod>;
    }

    template <class T> T* ptr() const
    {
        if (kind_ == Kind::Scalar)
            return const_cast<T*>(storage_.scalar.ptr<T>());
        if (kind_ == Kind::Boxed) {
            if (const auto* actual = exactValuePayload<T>(storage_.value))
                return const_cast<T*>(actual);
            return storage_.value.ptr<T>();
        }
        // A mutable pointer must never escape to Program-owned literal bytes.
        if constexpr (std::is_same_v<T, core::pmr::String> || std::is_same_v<T, ScriptMethod>)
            return is<T>() ? boxed().ptr<T>() : nullptr;
        else
            return nullptr;
    }

    // reflection::object::ptr<T>() is an unchecked reinterpretation. A typed
    // scalar read must validate imported carriers, while inline scalars need
    // only their own tag check. Use this when a nullptr selects dynamic semantics.
    template <class T> const T* scalarPointer() const
    {
        static_assert(std::is_same_v<T, int> || std::is_same_v<T, std::int64_t>
            || std::is_same_v<T, double> || std::is_same_v<T, bool>);
        if (kind_ == Kind::Scalar)
            return storage_.scalar.ptr<T>();
        return boxedScalarPointer<T>();
    }

    Value& boxed() const
    {
        if (kind_ == Kind::Scalar)
            setValue(storage_.scalar.materialize());
        else if (kind_ == Kind::Literal)
            setValue(makeString(*storage_.literal));
        else if (kind_ == Kind::Method) {
            // Copy the receiver first: a quota failure must leave the slot intact.
            Value value{storage_.method};
            setValue(std::move(value));
        }
        return storage_.value;
    }

    // Preserve the boxing boundary and real carrier while reusing a cache
    // view admitted for this same-thread, callback-free construction interval.
    Value& boxed(const IntegerValueCacheBorrow& cache) const
    {
        if (kind_ == Kind::Scalar) {
            if (const auto* integer = storage_.scalar.ptr<int>();
                integer && intValueUsesCache(*integer)) {
                setValue(cache.makeCached(*integer));
                return storage_.value;
            }
        }
        return boxed();
    }

    // Consuming ABI edge: transfer an existing carrier rather than retain it.
    Value takeValue()
    {
        if (kind_ == Kind::Scalar)
            return storage_.scalar.materialize();
        return std::move(boxed());
    }

    Value takeValue(const IntegerValueCacheBorrow& cache)
    {
        if (kind_ == Kind::Scalar) {
            if (const auto* integer = storage_.scalar.ptr<int>();
                integer && intValueUsesCache(*integer))
                return cache.makeCached(*integer);
        }
        return takeValue();
    }

    const ScriptScalar* inlineScalar() const noexcept
    {
        return kind_ == Kind::Scalar ? &storage_.scalar : nullptr;
    }

    // Inspect an existing ABI carrier without materializing a scalar, literal
    // or method. Borrow qualifiers use this before admitting a closed interval.
    const Value* boxedCarrier() const noexcept
    {
        return kind_ == Kind::Boxed ? &storage_.value : nullptr;
    }

    Value* uniqueBoxed()
    {
        return kind_ == Kind::Boxed && storage_.value.hasUniqueStorage() ? &storage_.value
                                                                         : nullptr;
    }

    operator Value&() { return boxed(); }

    operator const Value&() const { return boxed(); }

private:
    // Defined at the runtime ABI boundary. Keep reflection/native-object
    // resolution out of every inline scalar reader and arithmetic handler.
    template <class T> ARP_API const T* boxedScalarPointer() const;

    void destroy() const noexcept
    {
        if (kind_ == Kind::Boxed)
            std::destroy_at(&storage_.value);
        else if (kind_ == Kind::Method)
            std::destroy_at(&storage_.method);
        // Scalar and literal are trivially destructible.
    }

    void setScalar(ScriptScalar scalar) noexcept
    {
        if (kind_ == Kind::Scalar) {
            storage_.scalar = scalar;
            return;
        }
        destroy();
        std::uninitialized_copy_n(&scalar, 1, &storage_.scalar);
        kind_ = Kind::Scalar;
    }

    void setValue(Value value) const noexcept
    {
        if (kind_ == Kind::Boxed) {
            storage_.value = std::move(value);
            return;
        }
        destroy();
        std::uninitialized_move_n(&value, 1, &storage_.value);
        kind_ = Kind::Boxed;
    }

    void moveFrom(ExecutionSlot& other) noexcept
    {
        if (other.kind_ == Kind::Scalar) {
            setScalar(other.storage_.scalar);
            return;
        }
        if (other.kind_ == Kind::Boxed) {
            setValue(std::move(other.storage_.value));
            return;
        }
        destroy();
        if (other.kind_ == Kind::Method)
            std::uninitialized_move_n(&other.storage_.method, 1, &storage_.method);
        else
            storage_.literal = other.storage_.literal;
        kind_ = other.kind_;
    }

    mutable Storage storage_;
    mutable Kind kind_ = Kind::Scalar;
};

// Borrowed arguments have one typed ABI; internal calls can preserve inline
// scalars while public/host/generator calls continue importing Value references.
class ExecutionArgumentsView final {
public:
    ExecutionArgumentsView(const core::pmr::Vector<Value>& args)
        : values_(args.data()), count_(args.size())
    {}

    ExecutionArgumentsView(const core::pmr::Vector<ExecutionSlot>& args)
        : slots_(args.data()), count_(args.size())
    {}

    // Transfer an already-proved call's stack suffix directly into the callee.
    // The suffix is removed before callee execution (or on admission failure).
    // No callback/suspension may occur while this transfer is incomplete.
    ExecutionArgumentsView(core::pmr::Vector<ExecutionSlot>& stack, std::size_t count)
        : count_(count), consumption_(&stack), prefix_(stack.size() - count)
    {
        assert(count <= stack.size());
        if (count != 0)
            slots_ = stack.data() + prefix_;
    }

    // A bound method's receiver precedes the same borrowed stack suffix. It
    // retains its exact Value carrier; only script arguments are consumed.
    ExecutionArgumentsView(
        core::pmr::Vector<ExecutionSlot>& stack, std::size_t count, const Value& receiver)
        : ExecutionArgumentsView(stack, count)
    {
        receiver_ = &receiver;
        ++count_;
    }

    ExecutionArgumentsView(const ExecutionArgumentsView&) = delete;
    ExecutionArgumentsView& operator=(const ExecutionArgumentsView&) = delete;

    ~ExecutionArgumentsView() { finishConsumption(); }

    std::size_t size() const noexcept { return count_; }

    // Non-consuming inspection for pure argument-prefix execution. Unlike
    // operator[], this never transfers the caller's suffix. Native/cv and
    // user-defined payloads retain the ordinary frame and reflection path.
    const int* ownedIntegerPointer(std::size_t index) const
    {
        assert(index < count_);
        if (receiver_ != nullptr) {
            if (index == 0u)
                return exactValuePayload<int>(*receiver_);
            --index;
        }
        if (slots_ != nullptr) {
            const auto& slot = slots_[index];
            if (const auto* scalar = slot.inlineScalar())
                return scalar->ptr<int>();
            const auto* carrier = slot.boxedCarrier();
            return carrier ? exactValuePayload<int>(*carrier) : nullptr;
        }
        return exactValuePayload<int>(values_[index]);
    }

    ExecutionSlot operator[](std::size_t index) const
    {
        assert(index < count_);
        if (receiver_ != nullptr) {
            if (index == 0u)
                return ExecutionSlot(*receiver_);
            --index;
        }
        if (consumption_ != nullptr)
            return std::move((*consumption_)[prefix_ + index]);
        return slots_ != nullptr ? slots_[index] : ExecutionSlot(values_[index]);
    }

    void finishConsumption() const noexcept
    {
        if (consumption_ != nullptr) {
            consumption_->resize(prefix_);
            consumption_ = nullptr;
        }
    }

private:
    const Value* values_ = nullptr;
    const ExecutionSlot* slots_ = nullptr;
    const Value* receiver_ = nullptr;
    std::size_t count_ = 0;
    mutable core::pmr::Vector<ExecutionSlot>* consumption_ = nullptr;
    std::size_t prefix_ = 0;
};

// Reuse the PMR vector's storage/algorithms, adding only explicit ABI edges.
// A generated fixed frame can use the same consuming construction algorithms
// as the growable interpreter stack, without an intermediate operand vector.
class ExecutionSlotStackView final {
public:
    ExecutionSlotStackView(ExecutionSlot* slots, std::size_t& size, std::size_t capacity)
        : slots_(slots), size_(size), capacity_(capacity)
    {
        assert(size <= capacity);
    }

    std::size_t size() const noexcept { return size_; }

    ExecutionSlot& operator[](std::size_t index) const
    {
        assert(index < size_);
        return slots_[index];
    }

    ExecutionSlot& back() const
    {
        assert(size_ != 0u);
        return slots_[size_ - 1u];
    }

    void pop_back()
    {
        assert(size_ != 0u);
        slots_[--size_] = ScriptScalar();
    }

    void resize(std::size_t count)
    {
        assert(count <= size_ && count <= capacity_);
        while (size_ > count)
            pop_back();
    }

private:
    ExecutionSlot* slots_;
    std::size_t& size_;
    std::size_t capacity_;
};

class ExecutionSlotBuffer final : public core::pmr::Vector<ExecutionSlot> {
    using Base = core::pmr::Vector<ExecutionSlot>;

public:
    using Base::Base;
    using Base::operator=;

    ExecutionSlotBuffer& operator=(const core::pmr::Vector<Value>& values)
    {
        clear();
        reserve(values.size());
        for (const auto& value : values)
            emplace_back(value);
        return *this;
    }

    core::pmr::Vector<Value> materialize() const
    {
        core::pmr::Vector<Value> values{&core::defaultCpuMemoryResource()};
        values.reserve(size());
        for (const auto& slot : *this)
            values.push_back(slot.boxed());
        return values;
    }

    template <class Operation>
    auto applyValueOperation(std::size_t operandCount, Operation&& operation)
    {
        // Only the consumed suffix crosses the boxed ABI. Earlier operands
        // keep their inline representation and their exact reference identity.
        const auto count = std::min(operandCount, size());
        const auto start = size() - count;
        core::pmr::Vector<Value> operands{&core::defaultCpuMemoryResource()};
        operands.reserve(count);
        for (std::size_t index = start; index < size(); ++index)
            operands.push_back((*this)[index].boxed());
        auto result = operation(operands);
        resize(start);
        for (auto& value : operands)
            emplace_back(std::move(value));
        return result;
    }
};

} // namespace arp::scriptvm

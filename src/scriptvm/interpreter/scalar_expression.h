#pragma once

#include "core/borrowed_lookup_cache.h"
#include "core/cpu_allocator.h"
#include "scriptvm/runtime/semantic_runtime.h"

#include <array>

namespace arp::scriptvm {

struct Function;

// Private, non-serialized plans derived from exact immutable instructions.
// A successful evaluation has no effects until its single terminal operation.
class ScalarExpressionPlan final {
public:
    enum class Operation : std::uint8_t {
        Local,
        Constant,
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
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        FloatConstant,
        Temporary,
        Length,
        Index,
        StringConstant,
        StringKeyIndex,
        MethodLeaf,
        ClosureLeaf
    };
    enum class Terminal : std::uint8_t {
        Store,
        BranchFalse,
        BranchTrue,
        Return,
        AppendString,
        Push,
        IndexedStore,
        IndexedLoop,
        ConstructionOperands,
        ConstructionLoop,
        MethodLoop,
        ClosureLoop
    };

    struct Node {
        Operation operation = Operation::Constant;
        int operand = 0;
        core::Int32Divisor divisor;
        enum class IndexKind : std::uint8_t { Constant, Local, LocalRemainder };

        struct IndexOperand {
            int operand;
            IndexKind kind;
            bool dictionary;
        };

        struct StringKeyOperand {
            int keys;
            int index;
        };

        struct CallOperand {
            int targetLocal;
            int sourcePc;
        };

        static_assert(sizeof(IndexOperand) <= sizeof(double));
        static_assert(sizeof(StringKeyOperand) == sizeof(IndexOperand));
        static_assert(sizeof(CallOperand) == sizeof(double));

        // Literal payloads and index descriptors are mutually exclusive.
        union {
            double floating = 0.0;
            IndexOperand index;
            StringKeyOperand stringKey;
            CallOperand call;
            const core::pmr::String* string;
        };
    };

    // The fixed three/five-node scalar handlers have at most three operands.
    // Bound reads are admitted only for a non-suspending, call-free self edge.
    struct BoundIntSlots {
        std::array<const int*, 3> pointers{};

        struct Slot {
            const int* pointer;

            template <class T> const T* scalarPointer() const
            {
                static_assert(std::is_same_v<T, int>);
                return pointer;
            }
        };

        Slot operator[](std::size_t index) const { return {pointers[index]}; }
    };

    struct Entry {
        int offset = 0;
        int count = 0;
        int cost = 0;
        int lastPc = 0;
        int nextPc = 0;
        int destination = 0;
        Terminal terminal = Terminal::Return;
        int stepLocal = -1;
        int stepConstant = 0;
        bool subtractStep = false;
        bool floating = false;
        bool boundString = false;
        bool calculationLoop = false;
        int calculationOffset = 0;
        int calculationCount = 0;
        int leafCost = 0;
        int leafFrameValues = 0;
        int appendLocal = -1;
        int appendLiteralPc = -1;
        int stringSource = -1;
        int listDestination = -1;
        int dictionaryDestination = -1;
        int loopCondition = -1;
        int conditionalBody = -1;
        int conditionalTail = -1;
        int conditionalBranch = -1;
        int rangeBody = -1;
        bool (*evaluator)(const Node*, const ExecutionSlotBuffer&, int&) = nullptr;
        bool (*floatEvaluator)(const Node*, const ExecutionSlotBuffer&, double&) = nullptr;
        bool (*boundIntEvaluator)(const Node*, const BoundIntSlots&, int&) = nullptr;
    };

    struct Calculation {
        Node lhs, rhs;
        Operation operation = Operation::Add;
    };

    core::pmr::Vector<Node> nodes{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Entry> entries{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Calculation> calculations{&core::defaultCpuMemoryResource()};
    std::size_t scratchSize = 0;
    bool argumentLeaf = false;

    // A pure parameter comparison followed by a bare return can complete in
    // the original logical scope without preparing a physical operand frame.
    // The other edge resumes at the original branch destination, not PC zero.
    class GuardedReturn final {
    public:
        ARP_API static GuardedReturn prepare(
            const Function& function, const ScalarExpressionPlan& plan);

        bool valid() const noexcept { return compare_ != nullptr; }

        bool evaluate(const ExecutionArgumentsView& arguments, bool& returns) const
        {
            for (std::size_t index = 0; index < arguments.size(); ++index)
                if (!arguments.ownedIntegerPointer(index))
                    return false;
            const int lhs =
                leftLocal_ < 0 ? leftConstant_ : *arguments.ownedIntegerPointer(leftLocal_);
            const int rhs =
                rightLocal_ < 0 ? rightConstant_ : *arguments.ownedIntegerPointer(rightLocal_);
            returns = compare_(lhs, rhs) == returnWhenTrue_;
            return true;
        }

        ExecutionSlot result(const ExecutionArgumentsView& arguments) const
        {
            return resultLocal_ < 0 ? ExecutionSlot(ScriptScalar(resultConstant_))
                                    : arguments[resultLocal_];
        }

        int cost() const noexcept { return prefixCost_ + 2; }

        int prefixCost() const noexcept { return prefixCost_; }

        int resumePc() const noexcept { return resumePc_; }

        int lastPc() const noexcept { return lastPc_; }

    private:
        bool (*compare_)(const int&, const int&) = nullptr;
        int leftLocal_ = -1, rightLocal_ = -1;
        int leftConstant_ = 0, rightConstant_ = 0;
        int resultLocal_ = -1, resultConstant_ = 0;
        int prefixCost_ = 0, resumePc_ = 0, lastPc_ = 0;
        bool returnWhenTrue_ = true;
    } guardedReturn;

    // Local self-contained integer spans can retain a checked frame even
    // when exception edges prevent a whole-function operand-stack proof.
    bool independentStackSpans = false;
    // Immutable frame-entry selection keeps method gates out of other functions.
    bool hasMethodLoop = false;
    bool hasClosureLoop = false;

    // Function-level proof; no additional per-PC entry storage. Only exact
    // parameter/capture binary returns qualify, independently of caller CFG.
    class CaptureLeaf final {
    public:
        int argument = -1;
        int capture = -1;
        int cost = 0;
        Operation operation = Operation::Constant;
        bool capturedFirst = false;

        bool valid() const noexcept { return argument >= 0; }

        bool evaluate(const ExecutionSlot& parameter, const Value& captured, int& result) const
        {
            const int* argumentPayload = parameter.scalarPointer<int>();
            const int* capturePayload = checkedValuePayload<int>(captured);
            if (!argumentPayload || !capturePayload)
                return false;
            const int lhs = capturedFirst ? *capturePayload : *argumentPayload;
            const int rhs = capturedFirst ? *argumentPayload : *capturePayload;
            // A captured divisor is dynamic. A local operand descriptor keeps
            // the existing checked arithmetic path; it is never constant-folded.
            return applyOperation(operation, lhs, rhs, Node{Operation::Local}, result);
        }
    } captureLeaf;

    bool evaluateArgument(const ExecutionSlot& argument, int& value) const
    {
        const int* pointer = argument.scalarPointer<int>();
        if (!pointer)
            return false;
        const auto* expression = nodes.data() + entries[0].offset;
        switch (expression[2].operation) {
        case Operation::Add:
            return applyConstant<Operation::Add>(
                *pointer, expression[1].operand, expression[1], value);
        case Operation::Sub:
            return applyConstant<Operation::Sub>(
                *pointer, expression[1].operand, expression[1], value);
        case Operation::Mul:
            return applyConstant<Operation::Mul>(
                *pointer, expression[1].operand, expression[1], value);
        case Operation::Div:
            return applyConstant<Operation::Div>(
                *pointer, expression[1].operand, expression[1], value);
        case Operation::Mod:
            return applyConstant<Operation::Mod>(
                *pointer, expression[1].operand, expression[1], value);
        default:
            return false;
        }
    }

    // Select precompiled interpreter handlers from exact expression shape.
    // No machine code generation, script function specialization or AOT call.
    void specialize(Entry& entry)
    {
        const auto* expression = nodes.data() + entry.offset;
        if (entry.count == 3 && expression[1].operation == Operation::StringConstant) {
            entry.evaluator = stringComparisonEvaluator(expression[2].operation);
            if (entry.evaluator)
                return;
        }
        const bool scalarOnly = std::none_of(nodes.begin() + entry.offset,
            nodes.begin() + entry.offset + entry.count,
            [](const Node& node) {
                return node.operation == Operation::Length || node.operation == Operation::Index
                    || node.operation == Operation::StringKeyIndex;
            });
        if (entry.floating) {
            if (scalarOnly)
                specializeTyped<double, true>(entry);
            else
                specializeTyped<double, false>(entry);
        } else {
            if (scalarOnly)
                specializeTyped<int, true>(entry);
            else
                specializeTyped<int, false>(entry);
        }
        // Bound readers can replace an Index operand with its current checked
        // element pointer. Length and float payloads do not enter this handler.
        if (std::any_of(nodes.begin() + entry.offset,
                nodes.begin() + entry.offset + entry.count,
                [](const Node& node) {
                    return node.operation == Operation::Length
                        || node.operation == Operation::FloatConstant
                        || node.operation == Operation::StringConstant;
                }))
            entry.boundIntEvaluator = nullptr;
        if (entry.evaluator || entry.floatEvaluator)
            return;
        // Lower the same RPN into explicit operands once. Runtime visits only
        // arithmetic operations; literal/local pushes and stack bookkeeping
        // disappear. Temporary references always point to an earlier result.
        entry.calculationOffset = static_cast<int>(calculations.size());
        core::pmr::Vector<Node> operands{&core::defaultCpuMemoryResource()};
        lowerOperands(entry, operands);
        assert(operands.size() == 1);
    }

    // The same postfix lowering serves single-result expressions and multiple
    // construction operands. Bare local roots retain their original carriers.
    template <bool CallOperands = false>
    void lowerOperands(Entry& entry, core::pmr::Vector<Node>& operands)
    {
        for (int i = entry.offset; i != entry.offset + entry.count; ++i) {
            const auto& node = nodes[i];
            if (isOperand(node.operation)
                || (CallOperands
                    && (node.operation == Operation::MethodLeaf
                        || node.operation == Operation::ClosureLeaf))) {
                operands.push_back(node);
                continue;
            }
            assert(operands.size() >= 2);
            const auto rhs = operands.back();
            operands.pop_back();
            const auto lhs = operands.back();
            operands.pop_back();
            calculations.push_back({lhs, rhs, node.operation});
            operands.push_back({Operation::Temporary, entry.calculationCount++});
        }
        scratchSize = std::max(scratchSize, static_cast<std::size_t>(entry.calculationCount));
    }

    template <class T, bool ScalarOnly> void specializeTyped(Entry& entry) const
    {
        const auto* expression = nodes.data() + entry.offset;
        assert(std::none_of(expression, expression + entry.count, [](const auto& node) {
            return node.operation == Operation::MethodLeaf
                || node.operation == Operation::ClosureLeaf;
        }));
        const auto operand = [](Operation op) {
            return isOperand(op);
        };
        auto& evaluator = [&]() -> Evaluator<T>& {
            if constexpr (std::is_same_v<T, int>)
                return entry.evaluator;
            else
                return entry.floatEvaluator;
        }();
        if (entry.count == 1 && expression[0].operation == Operation::Length) {
            evaluator = &singleOperand<T>;
        } else if (entry.count == 3 && operand(expression[0].operation)
            && operand(expression[1].operation)) {
            evaluator = singleEvaluator<T, ScalarOnly>(expression[2].operation);
            if constexpr (std::is_same_v<T, int>)
                entry.boundIntEvaluator =
                    singleEvaluator<int, true, BoundIntSlots>(expression[2].operation);
        } else if (entry.count == 5 && operand(expression[0].operation)
            && operand(expression[1].operation) && operand(expression[3].operation)) {
            switch (expression[2].operation) {
            case Operation::Add:
                specializeChain<Operation::Add, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Sub:
                specializeChain<Operation::Sub, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Mul:
                specializeChain<Operation::Mul, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Div:
                specializeChain<Operation::Div, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Mod:
                specializeChain<Operation::Mod, T, ScalarOnly>(entry, expression[4].operation);
                break;
            default:
                break;
            }
        } else if (entry.count == 5 && operand(expression[0].operation)
            && operand(expression[1].operation) && operand(expression[2].operation)) {
            switch (expression[3].operation) {
            case Operation::Add:
                specializeRightChain<Operation::Add, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Sub:
                specializeRightChain<Operation::Sub, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Mul:
                specializeRightChain<Operation::Mul, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Div:
                specializeRightChain<Operation::Div, T, ScalarOnly>(entry, expression[4].operation);
                break;
            case Operation::Mod:
                specializeRightChain<Operation::Mod, T, ScalarOnly>(entry, expression[4].operation);
                break;
            default:
                break;
            }
        }
    }

    bool evaluateSlots(
        const Entry& entry, const ExecutionSlotBuffer& locals, int* scratch, int& value) const
    {
        if (entry.evaluator)
            return entry.evaluator(nodes.data() + entry.offset, locals, value);
        if (entry.calculationCount)
            return evaluateCalculations(entry, locals, scratch, value);
        return evaluate(
            entry,
            [&locals](int slot) {
                return locals[static_cast<std::size_t>(slot)].scalarPointer<int>();
            },
            scratch,
            value);
    }

    // Caller proves the current prefix plus original temporary stack demand
    // fits the checked frame, and retains quota executions on original PCs.
    ARP_API bool executeIndependent(const Entry& entry,
        ExecutionSlotBuffer& locals,
        ExecutionSlotBuffer& stack,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;
    ARP_API bool executeConstructionOperands(const Entry& entry,
        const ExecutionSlotBuffer& locals,
        ExecutionSlotBuffer& stack,
        int* scratch,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;
    ARP_API bool repeatConstruction(const Entry& entry,
        ExecutionSlotBuffer& locals,
        ExecutionSlotBuffer& stack,
        int* scratch,
        PrivateDictionaryWriteScratch& targets,
        std::size_t limit,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;

    bool prepareIndexedUpdate(const Entry& entry,
        const ExecutionSlotBuffer& locals,
        ScriptSemanticRuntime::IntegerIndexUpdate& update,
        int& value) const
    {
        assert(entry.terminal == Terminal::IndexedStore && entry.count == 0);
        int source;
        // IndexedStore has no scalar-local terminal or independent loop step.
        // Its mutually exclusive payload uses destination for base, stepLocal
        // for index; ordinary scalar entries retain their existing layout.
        if (!update.bind(locals[static_cast<std::size_t>(entry.destination)],
                locals[static_cast<std::size_t>(entry.stepLocal)],
                source))
            return false;
        const auto* expression = nodes.data() + entry.offset;
        return applyOperation(
            expression[1].operation, source, expression[0].operand, expression[0], value);
    }

    // Prepare the original independent step and virtual post-store condition.
    // Both dispatchers use this same pure guard before committing an entry.
    bool prepareTail(const Entry& entry,
        const ExecutionSlotBuffer& locals,
        ScriptScalar bodyValue,
        int& stepValue,
        bool& repeat) const
    {
        if (entry.stepLocal >= 0) {
            const int* current =
                locals[static_cast<std::size_t>(entry.stepLocal)].scalarPointer<int>();
            if (!current)
                return false;
            const auto update = entry.subtractStep
                ? ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Sub>(
                      *current, entry.stepConstant)
                : ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Add>(
                      *current, entry.stepConstant);
            if (!update.ok())
                return false;
            stepValue = static_cast<int>(update.value.integer());
        }
        repeat = true;
        if (entry.loopCondition < 0)
            return true;
        const auto& condition = entries[static_cast<std::size_t>(entry.loopCondition)];
        const auto* operands = nodes.data() + condition.offset;
        const auto local = [&](int slot, int& value) {
            if (slot == entry.stepLocal) {
                value = stepValue;
                return true;
            }
            if (slot == entry.destination && entry.terminal == Terminal::Store) {
                if (bodyValue.kind() != ScriptScalar::Kind::Int)
                    return false;
                value = static_cast<int>(bodyValue.integer());
                return true;
            }
            const int* pointer = locals[static_cast<std::size_t>(slot)].scalarPointer<int>();
            if (!pointer)
                return false;
            value = *pointer;
            return true;
        };
        int lhs, rhs = operands[1].operand;
        if (!local(operands[0].operand, lhs))
            return false;
        if (operands[1].operation == Operation::Local && !local(operands[1].operand, rhs))
            return false;
        repeat = lhs < rhs;
        return true;
    }

    bool evaluateFloatSlots(
        const Entry& entry, const ExecutionSlotBuffer& locals, double* scratch, double& value) const
    {
        if (entry.floatEvaluator)
            return entry.floatEvaluator(nodes.data() + entry.offset, locals, value);
        if (entry.calculationCount)
            return evaluateCalculations(entry, locals, scratch, value);
        return evaluate(
            entry,
            [&locals](int slot) {
                return locals[static_cast<std::size_t>(slot)].scalarPointer<double>();
            },
            scratch,
            value);
    }

    // A bounded interpreter handler: execute the same original span once per
    // iteration, retaining all checked arithmetic and exact budget/PC updates.
    // No calls, suspension, container access or carrier replacement can occur
    // between binding these pointers and the next return to the VM dispatcher.
    ARP_API bool repeatInteger(const Entry& entry,
        ExecutionSlotBuffer& locals,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;
    ARP_API bool repeatCalculations(const Entry& entry,
        ExecutionSlotBuffer& locals,
        int* scratch,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;

    // The VM supplies the actual admitted leaf, including its logical call and
    // temporary argument/callee carriers. This transaction shares the existing
    // checked body/tail and PC/budget commit with call-free closed intervals.
    // A closed leaf interval may borrow only actual owned primitive locals.
    // Native/cv/module reads retain ordinary dispatch and its host boundaries.
    static bool ownedInteger(const ExecutionSlot& local)
    {
        if (const auto* scalar = local.inlineScalar())
            return scalar->ptr<int>() != nullptr;
        const auto* value = local.boxedCarrier();
        return value && exactValuePayload<int>(*value);
    }

    bool admitsClosedLeafLocals(const Entry& entry, const ExecutionSlotBuffer& locals) const
    {
        const auto& call = nodes[static_cast<std::size_t>(entry.rangeBody)];
        if (!ownedInteger(locals[static_cast<std::size_t>(call.operand)]))
            return false;
        const auto qualifies = [&](const Node& node) {
            return node.operation != Operation::Local
                || ownedInteger(locals[static_cast<std::size_t>(node.operand)]);
        };
        for (int i = 0; i < entry.calculationCount; ++i) {
            const auto& calculation = calculations[entry.calculationOffset + i];
            if (!qualifies(calculation.lhs) || !qualifies(calculation.rhs))
                return false;
        }
        const auto& condition = entries[static_cast<std::size_t>(entry.loopCondition)];
        return std::all_of(nodes.begin() + condition.offset,
            nodes.begin() + condition.offset + condition.count,
            qualifies);
    }

    template <class Call>
    bool repeatCall(const Entry& entry,
        ExecutionSlotBuffer& locals,
        int* scratch,
        int leafCost,
        std::int64_t& remaining,
        int& pc,
        int& lastPc,
        Call call) const
    {
        assert((entry.terminal == Terminal::MethodLoop || entry.terminal == Terminal::ClosureLoop)
            && entry.rangeBody >= 0 && entry.count == 0);
        auto joined = entry;
        joined.cost += leafCost;
        int* destination = inlineInteger(locals, joined.destination);
        int value = 0;
        BoundLoop loop(*this, joined, locals, &value);
        if (!destination || !loop.valid())
            return false;
        return loop.run(
            remaining,
            pc,
            lastPc,
            [&] {
                int called;
                if (!call(called))
                    return false;
                if (!joined.calculationCount) {
                    value = called;
                    return true;
                }
                if (!evaluateCalculationResultsWithReader(
                        joined, scratch, [&](const Node& node, int& result) {
                            if (node.operation == Operation::MethodLeaf
                                || node.operation == Operation::ClosureLeaf) {
                                result = called;
                                return true;
                            }
                            return operand(node, locals, result);
                        }))
                    return false;
                value = scratch[joined.calculationCount - 1];
                return true;
            },
            [&] {
                *destination = value;
                return true;
            });
    }

    ARP_API bool repeatString(const Entry& entry,
        ExecutionSlotBuffer& locals,
        std::string_view suffix,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;
    ARP_API bool repeatConditional(const Entry& entry,
        ExecutionSlotBuffer& locals,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;
    // Store/rangeBody and conditionalTail encode a separately proved index
    // definition -> existing-node update -> scalar tail closed edge. RangeNext
    // entries retain their independent rangeBody payload and have count == 0.
    ARP_API bool repeatIndexed(const Entry& prefix,
        ExecutionSlotBuffer& locals,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;

    // On failure the caller replays the original instructions, preserving the
    // exact fault PC, consumed operands and diagnostic. Reads have no effects.
    template <class ReadLocal, class T>
    bool evaluate(const Entry& entry, ReadLocal read, T* scratch, T& value) const
    {
        int depth = 0;
        for (int index = entry.offset; index != entry.offset + entry.count; ++index) {
            const auto& node = nodes[static_cast<std::size_t>(index)];
            if (node.operation == Operation::Constant) {
                scratch[depth++] = node.operand;
                continue;
            }
            if (node.operation == Operation::FloatConstant) {
                if constexpr (std::is_same_v<T, double>) {
                    scratch[depth++] = node.floating;
                    continue;
                } else
                    return false;
            }
            if (node.operation == Operation::Local) {
                const T* local = read(node.operand);
                if (!local)
                    return false;
                scratch[depth++] = *local;
                continue;
            }
            if (node.operation == Operation::Length || node.operation == Operation::Index)
                return false;
            const T rhs = scratch[--depth], lhs = scratch[depth - 1];
            T result;
            if (!applyOperation(node.operation, lhs, rhs, nodes[index - 1], result))
                return false;
            scratch[depth - 1] = result;
        }
        value = scratch[0];
        return true;
    }

private:
    template <class Access, Operation Indexed>
    bool repeatBorrowedInteger(const Entry& entry,
        ExecutionSlotBuffer& locals,
        std::int64_t& remaining,
        int& pc,
        int& lastPc) const;

    // A nested key read is admitted only by an immutable call-free self-edge
    // proof. The list retains the real key carriers; dictionary topology and
    // stock ordering cannot change during this borrow. Cache real Value
    // addresses, never detached strings or mapped payloads.
    class StringKeyIndexAccess final {
    public:
        bool bindReadonly(const Node& node, const ExecutionSlotBuffer& locals)
        {
            cache_.clear();
            const auto& base = locals[static_cast<std::size_t>(node.operand)];
            const auto& keys = locals[static_cast<std::size_t>(node.stringKey.keys)];
            // Check kinds before boxed(): Literal/Method refusals are pure.
            if (!base.is<arp::reflection::hdict>() || !keys.is<arp::reflection::hlist>()
                || base.boxed().is_native_reference() || keys.boxed().is_native_reference())
                return false;
            dictionary_ = checkedValuePayload<arp::reflection::hdict>(base.boxed());
            keys_ = checkedValuePayload<arp::reflection::hlist>(keys.boxed());
            index_ = locals[static_cast<std::size_t>(node.stringKey.index)].scalarPointer<int>();
            if (!dictionary_ || !keys_ || !index_)
                return false;
            const auto builtinString = [](const Value& value) {
                if (!checkedValuePayload<core::pmr::String>(value) || value.is_native_reference())
                    return false;
                const auto* metadata = value.class_();
                return metadata && metadata->hasBuiltinOrdering();
            };
            if (!std::all_of(dictionary_->begin(),
                    dictionary_->end(),
                    [&](const auto& item) {
                        return builtinString(item.first);
                    })
                || !std::all_of(keys_->begin(), keys_->end(), builtinString))
                return false;
            node_ = &node;
            return true;
        }

        const int* read() const
        {
            const auto remainder = ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Mod>(
                *index_, node_->divisor);
            if (!remainder.ok())
                return nullptr;
            const int index = static_cast<int>(remainder.value.integer());
            if (index < 0 || static_cast<std::size_t>(index) >= keys_->size())
                return nullptr;
            const Value* key = &(*keys_)[static_cast<std::size_t>(index)];
            if (!checkedValuePayload<core::pmr::String>(*key))
                return nullptr;
            const Value* element;
            if (!cache_.find(key, element)) {
                const auto found = dictionary_->find(*key);
                element = found == dictionary_->end() ? nullptr : &found->second;
                cache_.insert(key, element);
            }
            // Missing and actual type/native mismatches replay the original
            // LoadIndex/checked arithmetic, including nil and diagnostic PCs.
            return element ? checkedOwnedValuePayload<int>(*element) : nullptr;
        }

    private:
        const Node* node_ = nullptr;
        const arp::reflection::hdict* dictionary_ = nullptr;
        const arp::reflection::hlist* keys_ = nullptr;
        const int* index_ = nullptr;
        // Same explicit 64-byte entry-storage budget as integer lookup. A key
        // is a borrowed Value address (not a 24-byte string_view). Eviction
        // affects only hit rate; every miss retains the real hdict::find.
        static constexpr std::size_t lookupStorageBytes = 64u;
        static constexpr std::size_t lookupEntries =
            lookupStorageBytes / core::BorrowedLookupCache<const Value*, Value, 0u>::entryBytes;
        mutable core::BorrowedLookupCache<const Value*, Value, lookupEntries> cache_;
    };

    // Borrow only the unchanged container and index source. Each read still
    // checks remainder, bounds/missing key and actual element type. No owner,
    // scratch allocation or detached copy of Value/container storage is created.
    class IntegerIndexAccess final {
    public:
        bool bind(const Node& node, const ExecutionSlotBuffer& locals)
        {
            cache_.clear();
            readonly_ = false;
            localKey_ = nullptr;
            list_ = nullptr;
            dictionary_ = nullptr;
            node_ = &node;
            const auto& base = locals[static_cast<std::size_t>(node.operand)];
            if (node.index.dictionary) {
                if (!base.is<arp::reflection::hdict>()
                    || !arp::reflection::type_of<int>()->class_()->hasBuiltinOrdering())
                    return false;
                dictionary_ = base.ptr<arp::reflection::hdict>();
                if (!dictionary_)
                    return false;
            } else {
                if (!base.is<arp::reflection::hlist>())
                    return false;
                list_ = base.ptr<arp::reflection::hlist>();
                if (!list_)
                    return false;
            }
            index_ = node.index.kind == Node::IndexKind::Constant
                ? &node.index.operand
                : locals[static_cast<std::size_t>(node.index.operand)].scalarPointer<int>();
            if (index_ && dictionary_ && node.index.kind == Node::IndexKind::Local) {
                const auto& source = locals[static_cast<std::size_t>(node.index.operand)];
                if (!source.inlineScalar())
                    localKey_ = &source.boxed();
            }
            return index_ != nullptr;
        }

        bool bindReadonly(const Node& node, const ExecutionSlotBuffer& locals)
        {
            if (!bind(node, locals))
                return false;
            if (dictionary_
                && !std::all_of(dictionary_->begin(), dictionary_->end(), [](const auto& item) {
                       return item.first.template is<int>() && !item.first.is_native_reference();
                   }))
                return false;
            readonly_ = true;
            return true;
        }

        // Valid only for the exact borrowed span that writes independent scalar
        // locals and cannot call, suspend, replace or mutate this container.
        // Qualify every actual element once; declared types are not evidence.
        bool bindStrings(const Node& node, const ExecutionSlotBuffer& locals)
        {
            if (!bindReadonly(node, locals))
                return false;
            const auto string = [](const Value& element) {
                return checkedValuePayload<core::pmr::String>(element) != nullptr;
            };
            if (dictionary_)
                return std::all_of(
                    dictionary_->begin(), dictionary_->end(), [&](const auto& element) {
                        return string(element.second);
                    });
            return std::all_of(list_->begin(), list_->end(), string);
        }

        const Value* readValue() const
        {
            int index = *index_;
            if (node_->index.kind == Node::IndexKind::LocalRemainder) {
                const auto result = ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Mod>(
                    index, node_->divisor);
                if (!result.ok())
                    return nullptr;
                index = static_cast<int>(result.value.integer());
            }
            const Value* element;
            if (dictionary_) {
                if (readonly_ && cache_.find(index, element))
                    return element;
                const auto* key = localKey_ ? localKey_ : findCachedIntValue(index);
                if (!key)
                    return nullptr;
                if (readonly_ && (!key->is<int>() || key->is_native_reference()))
                    return nullptr;
                const auto found = dictionary_->find(*key);
                element = found == dictionary_->end() ? nullptr : &found->second;
                if (readonly_)
                    cache_.insert(index, element);
            } else {
                if (index < 0 || static_cast<std::size_t>(index) >= list_->size())
                    return nullptr;
                element = &(*list_)[static_cast<std::size_t>(index)];
            }
            return element;
        }

        const int* read() const
        {
            const Value* element = readValue();
            return element ? checkedValuePayload<int>(*element) : nullptr;
        }

        const core::pmr::String* readString() const
        {
            const Value* element = readValue();
            if (!element)
                return nullptr;
            assert(element->is<core::pmr::String>());
            return checkedValuePayload<core::pmr::String>(*element);
        }

    private:
        const Node* node_ = nullptr;
        const int* index_ = nullptr;
        const Value* localKey_ = nullptr;
        const arp::reflection::hlist* list_ = nullptr;
        const arp::reflection::hdict* dictionary_ = nullptr;
        // Explicit stack-storage policy, independent of CPU cache-line size or
        // dictionary cardinality. Every miss still uses the original hdict find.
        static constexpr std::size_t lookupStorageBytes = 64u;
        static constexpr std::size_t lookupEntries =
            lookupStorageBytes / core::BorrowedLookupCache<int, Value, 0u>::entryBytes;
        mutable core::BorrowedLookupCache<int, Value, lookupEntries> cache_;
        bool readonly_ = false;
    };

    static int* inlineInteger(ExecutionSlotBuffer& locals, int slot)
    {
        const auto* scalar = locals[static_cast<std::size_t>(slot)].inlineScalar();
        return scalar ? const_cast<int*>(scalar->ptr<int>()) : nullptr;
    }

    // Shared transaction for exact call-free self edges. All pure guards run
    // before the body commits. Only committed iterations charge their complete
    // original cost; a failed guard returns to the original instruction replay.
    class BoundLoop final {
    public:
        BoundLoop(const ScalarExpressionPlan& plan,
            const Entry& entry,
            ExecutionSlotBuffer& locals,
            const int* destinationValue)
            : entry_(entry)
        {
            step_ = inlineInteger(locals, entry.stepLocal);
            const auto local = [&](int slot) -> const int* {
                if (slot == entry.destination)
                    return destinationValue;
                if (slot == entry.stepLocal)
                    return &stepValue_;
                return locals[static_cast<std::size_t>(slot)].scalarPointer<int>();
            };
            const auto& condition = plan.entries[static_cast<std::size_t>(entry.loopCondition)];
            exitPc_ = condition.destination;
            const auto* operands = plan.nodes.data() + condition.offset;
            lhs_ = local(operands[0].operand);
            rhs_ = operands[1].operation == Operation::Constant ? &operands[1].operand
                                                                : local(operands[1].operand);
        }

        bool valid() const { return step_ && lhs_ && rhs_; }

        bool prepare(int& stepValue, bool& repeat)
        {
            const auto update = entry_.subtractStep
                ? ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Sub>(
                      *step_, entry_.stepConstant)
                : ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Add>(
                      *step_, entry_.stepConstant);
            if (!update.ok())
                return false;
            stepValue_ = stepValue = static_cast<int>(update.value.integer());
            repeat = *lhs_ < *rhs_;
            return true;
        }

        template <class Prepare, class Commit>
        bool run(std::int64_t& remaining, int& pc, int& lastPc, Prepare prepare, Commit commit)
        {
            return run(remaining, pc, lastPc, prepare, commit, [&] {
                return entry_.cost;
            });
        }

        template <class Prepare, class Commit, class Cost>
        bool run(std::int64_t& remaining,
            int& pc,
            int& lastPc,
            Prepare prepare,
            Commit commit,
            Cost cost)
        {
            bool progressed = false;
            while (remaining < 0 || remaining >= entry_.cost) {
                if (!prepare())
                    break;
                const int charged = cost();
                if (remaining >= 0 && remaining < charged)
                    break;
                int stepValue;
                bool repeat;
                if (!this->prepare(stepValue, repeat))
                    break;
                if (!commit())
                    break;
                *step_ = stepValue_;
                if (remaining > 0)
                    remaining -= charged;
                lastPc = entry_.lastPc;
                pc = repeat ? entry_.nextPc : exitPc_;
                progressed = true;
                if (!repeat)
                    break;
            }
            return progressed;
        }

    private:
        const Entry& entry_;
        int* step_ = nullptr;
        const int* lhs_ = nullptr;
        const int* rhs_ = nullptr;
        int stepValue_ = 0;
        int exitPc_ = 0;
    };

    static bool isOperand(Operation op) noexcept
    {
        return op == Operation::Local || op == Operation::Constant || op == Operation::FloatConstant
            || op == Operation::Length || op == Operation::Index || op == Operation::StringConstant
            || op == Operation::StringKeyIndex;
    }

    static bool stringOperand(
        const Node& node, const ExecutionSlotBuffer& locals, std::string_view& text)
    {
        if (node.operation == Operation::StringConstant) {
            text = *node.string;
            return true;
        }
        if (node.operation == Operation::Local)
            return locals[static_cast<std::size_t>(node.operand)].stringView(text);
        if (node.operation == Operation::Index) {
            IntegerIndexAccess access;
            if (!access.bind(node, locals))
                return false;
            const Value* value = access.readValue();
            if (!value || !value->is<core::pmr::String>())
                return false;
            const auto* string = value->ptr<core::pmr::String>();
            if (!string)
                return false;
            text = *string;
            return true;
        }
        return false;
    }

    template <SemanticCompareOperation Op>
    static bool stringComparison(const Node* nodes, const ExecutionSlotBuffer& locals, int& value)
    {
        std::string_view lhs, rhs;
        if (!stringOperand(nodes[0], locals, lhs) || !stringOperand(nodes[1], locals, rhs))
            return false;
        value = ScriptSemanticRuntime::comparePayload<Op>(lhs, rhs);
        return true;
    }

    using StringComparisonEvaluator = bool (*)(const Node*, const ExecutionSlotBuffer&, int&);

    static StringComparisonEvaluator stringComparisonEvaluator(Operation operation)
    {
        switch (operation) {
        case Operation::Equal:
            return &stringComparison<SemanticCompareOperation::Equal>;
        case Operation::NotEqual:
            return &stringComparison<SemanticCompareOperation::NotEqual>;
        case Operation::Less:
            return &stringComparison<SemanticCompareOperation::Less>;
        case Operation::LessEqual:
            return &stringComparison<SemanticCompareOperation::LessEqual>;
        case Operation::Greater:
            return &stringComparison<SemanticCompareOperation::Greater>;
        case Operation::GreaterEqual:
            return &stringComparison<SemanticCompareOperation::GreaterEqual>;
        default:
            return nullptr;
        }
    }

    template <class T>
    static bool singleOperand(const Node* nodes, const ExecutionSlotBuffer& locals, T& result)
    {
        return operand(nodes[0], locals, result);
    }

    template <class T>
    static bool applyOperation(Operation operation, T lhs, T rhs, const Node& right, T& result)
    {
        switch (operation) {
        case Operation::Add:
            return apply<Operation::Add>(lhs, rhs, result);
        case Operation::Sub:
            return apply<Operation::Sub>(lhs, rhs, result);
        case Operation::Mul:
            return apply<Operation::Mul>(lhs, rhs, result);
        case Operation::Div:
            return applyConstant<Operation::Div>(lhs, rhs, right, result);
        case Operation::Mod:
            return applyConstant<Operation::Mod>(lhs, rhs, right, result);
        case Operation::BitAnd:
            return apply<Operation::BitAnd>(lhs, rhs, result);
        case Operation::BitOr:
            return apply<Operation::BitOr>(lhs, rhs, result);
        case Operation::BitXor:
            return apply<Operation::BitXor>(lhs, rhs, result);
        case Operation::BitClear:
            return apply<Operation::BitClear>(lhs, rhs, result);
        case Operation::ShiftLeft:
            return apply<Operation::ShiftLeft>(lhs, rhs, result);
        case Operation::ShiftRight:
            return apply<Operation::ShiftRight>(lhs, rhs, result);
        case Operation::Pow:
            return apply<Operation::Pow>(lhs, rhs, result);
        case Operation::FloorDiv:
            return apply<Operation::FloorDiv>(lhs, rhs, result);
        case Operation::Equal:
            result =
                ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Equal>(lhs, rhs);
            return true;
        case Operation::NotEqual:
            result =
                ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::NotEqual>(lhs, rhs);
            return true;
        case Operation::Less:
            result =
                ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Less>(lhs, rhs);
            return true;
        case Operation::LessEqual:
            result = ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::LessEqual>(
                lhs, rhs);
            return true;
        case Operation::Greater:
            result =
                ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Greater>(lhs, rhs);
            return true;
        case Operation::GreaterEqual:
            result = ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::GreaterEqual>(
                lhs, rhs);
            return true;
        default:
            return false;
        }
    }

    template <class T>
    bool evaluateCalculations(
        const Entry& entry, const ExecutionSlotBuffer& locals, T* scratch, T& value) const
    {
        if (!evaluateCalculationResults(entry, locals, scratch))
            return false;
        value = scratch[entry.calculationCount - 1];
        return true;
    }

    template <class T>
    static bool calculationOperand(
        const Node& node, const ExecutionSlotBuffer& locals, const T* scratch, T& result)
    {
        if (node.operation == Operation::Temporary) {
            result = scratch[node.operand];
            return true;
        }
        return operand(node, locals, result);
    }

    template <class T>
    bool evaluateCalculationResults(
        const Entry& entry, const ExecutionSlotBuffer& locals, T* scratch) const
    {
        return evaluateCalculationResultsWithReader(
            entry, scratch, [&](const Node& node, T& result) {
                return operand(node, locals, result);
            });
    }

    template <class T, class Read>
    bool evaluateCalculationResultsWithReader(const Entry& entry, T* scratch, Read read) const
    {
        const auto resolve = [&](const Node& node, T& result) {
            if (node.operation == Operation::Temporary) {
                result = scratch[node.operand];
                return true;
            }
            return read(node, result);
        };
        for (int i = 0; i != entry.calculationCount; ++i) {
            const auto& calculation = calculations[entry.calculationOffset + i];
            T lhs, rhs;
            if (!resolve(calculation.lhs, lhs) || !resolve(calculation.rhs, rhs)
                || !applyOperation(calculation.operation, lhs, rhs, calculation.rhs, scratch[i]))
                return false;
        }
        return true;
    }
    template <class T, class Slots = ExecutionSlotBuffer>
    using Evaluator = bool (*)(const Node*, const Slots&, T&);

    template <class T, bool ScalarOnly = false, class Slots = ExecutionSlotBuffer>
    static bool operand(const Node& node, const Slots& locals, T& value)
    {
        if (node.operation == Operation::Constant) {
            value = node.operand;
            return true;
        }
        if (node.operation == Operation::FloatConstant) {
            if constexpr (std::is_same_v<T, double>) {
                value = node.floating;
                return true;
            } else
                return false;
        }
        if constexpr (!ScalarOnly) {
            if (node.operation != Operation::Local)
                return collectionOperand(node, locals, value);
        } else
            assert(node.operation == Operation::Local);
        {
            const T* pointer =
                locals[static_cast<std::size_t>(node.operand)].template scalarPointer<T>();
            if (!pointer)
                return false;
            value = *pointer;
            return true;
        }
    }

    // Keep collection dispatch out of the small scalar operand reader, so
    // numeric handlers can inline their local/constant reads independently.
    template <class T>
    static bool collectionOperand(const Node& node, const ExecutionSlotBuffer& locals, T& value)
    {
        if (node.operation == Operation::Length) {
            if constexpr (std::is_same_v<T, int>) {
                const auto length = ScriptSemanticRuntime::lengthScalar(
                    locals[static_cast<std::size_t>(node.operand)]);
                if (!length.ok())
                    return false;
                value = static_cast<int>(length.value.integer());
                return true;
            } else
                return false;
        }
        if (node.operation == Operation::Index) {
            if constexpr (std::is_same_v<T, int>) {
                IntegerIndexAccess access;
                if (!access.bind(node, locals))
                    return false;
                const auto* pointer = access.read();
                if (!pointer)
                    return false;
                value = *pointer;
                return true;
            }
        }
        return false;
    }

    template <Operation Op, class T> static bool apply(T lhs, T rhs, T& value)
    {
        if constexpr (Op >= Operation::Equal) {
            constexpr auto comparison = static_cast<SemanticCompareOperation>(
                static_cast<int>(Op) - static_cast<int>(Operation::Equal));
            value = ScriptSemanticRuntime::comparePayload<comparison>(lhs, rhs);
            return true;
        } else {
            constexpr auto arithmetic = static_cast<SemanticBinaryOperation>(
                static_cast<int>(Op) - static_cast<int>(Operation::Add));
            const auto result = [&] {
                if constexpr (std::is_same_v<T, int>) {
                    if constexpr (Op <= Operation::BitClear)
                        return ScriptSemanticRuntime::intBinary<arithmetic>(lhs, rhs);
                    else {
                        ScalarOperationResult result;
                        if (!ScriptSemanticRuntime::scalarBinary(
                                arithmetic, ScriptScalar(lhs), ScriptScalar(rhs), result)
                            || result.value.kind() != ScriptScalar::Kind::Int)
                            result.code = ScriptDiagnosticCode::BadCast;
                        return result;
                    }
                } else {
                    if constexpr (Op >= Operation::BitAnd && Op <= Operation::ShiftRight) {
                        ScalarOperationResult result;
                        result.code = ScriptDiagnosticCode::BadCast;
                        return result;
                    } else
                        return ScriptSemanticRuntime::floatBinary<arithmetic>(lhs, rhs);
                }
            }();
            if (!result.ok())
                return false;
            if constexpr (std::is_same_v<T, int>)
                value = static_cast<int>(result.value.integer());
            else
                value = result.value.floating();
            return true;
        }
    }

    template <Operation Op, class T>
    static bool applyConstant(T lhs, T rhs, const Node& node, T& value)
    {
        if constexpr (std::is_same_v<T, int> && (Op == Operation::Div || Op == Operation::Mod)) {
            if (node.operation == Operation::Constant) {
                constexpr auto operation = Op == Operation::Div ? SemanticBinaryOperation::Div
                                                                : SemanticBinaryOperation::Mod;
                const auto result = ScriptSemanticRuntime::intBinary<operation>(lhs, node.divisor);
                if (!result.ok())
                    return false;
                value = static_cast<int>(result.value.integer());
                return true;
            }
        }
        return apply<Op>(lhs, rhs, value);
    }

    template <Operation Op, class T, bool ScalarOnly, class Slots = ExecutionSlotBuffer>
    static bool applyOperand(T lhs, const Node& node, const Slots& locals, T& value)
    {
        if constexpr (std::is_same_v<T, int> && std::is_same_v<Slots, BoundIntSlots>
            && (Op == Operation::Div || Op == Operation::Mod))
            return boundDivisionOperand<Op>(lhs, node, locals, value);
        T rhs;
        return operand<T, ScalarOnly>(node, locals, rhs)
            && applyConstant<Op>(lhs, rhs, node, value);
    }

    // Keep checked reciprocal division out of every expanded chain handler.
    // One compiled boundary shares the existing operand/checked algorithm.
    template <Operation Op>
    ARP_API static bool boundDivisionOperand(
        int lhs, const Node& node, const BoundIntSlots& locals, int& value);

    template <Operation Op, class T, bool ScalarOnly, class Slots = ExecutionSlotBuffer>
    static bool single(const Node* nodes, const Slots& locals, T& value)
    {
        T lhs;
        return operand<T, ScalarOnly>(nodes[0], locals, lhs)
            && applyOperand<Op, T, ScalarOnly>(lhs, nodes[1], locals, value);
    }

    template <Operation First,
        Operation Second,
        class T,
        bool ScalarOnly,
        class Slots = ExecutionSlotBuffer>
    static bool chain(const Node* nodes, const Slots& locals, T& value)
    {
        T first;
        return single<First, T, ScalarOnly>(nodes, locals, first)
            && applyOperand<Second, T, ScalarOnly>(first, nodes[3], locals, value);
    }

    template <Operation First,
        Operation Second,
        class T,
        bool ScalarOnly,
        class Slots = ExecutionSlotBuffer>
    static bool rightChain(const Node* nodes, const Slots& locals, T& value)
    {
        T lhs, rhs;
        // Preserve source order: read lhs, evaluate the complete RHS, then
        // apply the final checked operation. RHS is a computed value, so it
        // must never consume the literal divisor metadata from its last node.
        return operand<T, ScalarOnly>(nodes[0], locals, lhs)
            && single<First, T, ScalarOnly>(nodes + 1, locals, rhs)
            && apply<Second>(lhs, rhs, value);
    }

    template <class T, bool ScalarOnly, class Slots = ExecutionSlotBuffer>
    static Evaluator<T, Slots> singleEvaluator(Operation op)
    {
        switch (op) {
        case Operation::Add:
            return &single<Operation::Add, T, ScalarOnly, Slots>;
        case Operation::Sub:
            return &single<Operation::Sub, T, ScalarOnly, Slots>;
        case Operation::Mul:
            return &single<Operation::Mul, T, ScalarOnly, Slots>;
        case Operation::Div:
            return &single<Operation::Div, T, ScalarOnly, Slots>;
        case Operation::Mod:
            return &single<Operation::Mod, T, ScalarOnly, Slots>;
        case Operation::BitAnd:
            return &single<Operation::BitAnd, T, ScalarOnly, Slots>;
        case Operation::BitOr:
            return &single<Operation::BitOr, T, ScalarOnly, Slots>;
        case Operation::BitXor:
            return &single<Operation::BitXor, T, ScalarOnly, Slots>;
        case Operation::BitClear:
            return &single<Operation::BitClear, T, ScalarOnly, Slots>;
        case Operation::ShiftLeft:
            return &single<Operation::ShiftLeft, T, ScalarOnly, Slots>;
        case Operation::ShiftRight:
            return &single<Operation::ShiftRight, T, ScalarOnly, Slots>;
        case Operation::Pow:
            return &single<Operation::Pow, T, ScalarOnly, Slots>;
        case Operation::FloorDiv:
            return &single<Operation::FloorDiv, T, ScalarOnly, Slots>;
        case Operation::Equal:
            return &single<Operation::Equal, T, ScalarOnly, Slots>;
        case Operation::NotEqual:
            return &single<Operation::NotEqual, T, ScalarOnly, Slots>;
        case Operation::Less:
            return &single<Operation::Less, T, ScalarOnly, Slots>;
        case Operation::LessEqual:
            return &single<Operation::LessEqual, T, ScalarOnly, Slots>;
        case Operation::Greater:
            return &single<Operation::Greater, T, ScalarOnly, Slots>;
        case Operation::GreaterEqual:
            return &single<Operation::GreaterEqual, T, ScalarOnly, Slots>;
        default:
            return nullptr;
        }
    }

    template <Operation First, class T, bool ScalarOnly>
    void specializeChain(Entry& entry, Operation second) const
    {
        if constexpr (std::is_same_v<T, int>)
            entry.evaluator = chainEvaluator<First, T, ScalarOnly>(second);
        else
            entry.floatEvaluator = chainEvaluator<First, T, ScalarOnly>(second);
        if constexpr (std::is_same_v<T, int>)
            entry.boundIntEvaluator = chainEvaluator<First, int, true, BoundIntSlots>(second);
    }

    template <Operation First, class T, bool ScalarOnly>
    void specializeRightChain(Entry& entry, Operation second) const
    {
        if constexpr (std::is_same_v<T, int>)
            entry.evaluator =
                chainEvaluator<First, T, ScalarOnly, ExecutionSlotBuffer, true>(second);
        else
            entry.floatEvaluator =
                chainEvaluator<First, T, ScalarOnly, ExecutionSlotBuffer, true>(second);
        if constexpr (std::is_same_v<T, int>)
            entry.boundIntEvaluator = chainEvaluator<First, int, true, BoundIntSlots, true>(second);
    }

    template <Operation First, Operation Second, class T, bool ScalarOnly, class Slots, bool Right>
    static constexpr Evaluator<T, Slots> chainHandler()
    {
        if constexpr (Right)
            return &rightChain<First, Second, T, ScalarOnly, Slots>;
        else
            return &chain<First, Second, T, ScalarOnly, Slots>;
    }

    template <Operation First,
        class T,
        bool ScalarOnly,
        class Slots = ExecutionSlotBuffer,
        bool Right = false>
    static Evaluator<T, Slots> chainEvaluator(Operation op)
    {
        switch (op) {
        case Operation::Add:
            return chainHandler<First, Operation::Add, T, ScalarOnly, Slots, Right>();
        case Operation::Sub:
            return chainHandler<First, Operation::Sub, T, ScalarOnly, Slots, Right>();
        case Operation::Mul:
            return chainHandler<First, Operation::Mul, T, ScalarOnly, Slots, Right>();
        case Operation::Div:
            return chainHandler<First, Operation::Div, T, ScalarOnly, Slots, Right>();
        case Operation::Mod:
            return chainHandler<First, Operation::Mod, T, ScalarOnly, Slots, Right>();
        case Operation::Equal:
            return chainHandler<First, Operation::Equal, T, ScalarOnly, Slots, Right>();
        case Operation::NotEqual:
            return chainHandler<First, Operation::NotEqual, T, ScalarOnly, Slots, Right>();
        case Operation::Less:
            return chainHandler<First, Operation::Less, T, ScalarOnly, Slots, Right>();
        case Operation::LessEqual:
            return chainHandler<First, Operation::LessEqual, T, ScalarOnly, Slots, Right>();
        case Operation::Greater:
            return chainHandler<First, Operation::Greater, T, ScalarOnly, Slots, Right>();
        case Operation::GreaterEqual:
            return chainHandler<First, Operation::GreaterEqual, T, ScalarOnly, Slots, Right>();
        default:
            return nullptr;
        }
    }
};

} // namespace arp::scriptvm

#include "scriptvm/interpreter/scalar_expression.h"

#include "scriptvm/interpreter/bytecode.h"

namespace arp::scriptvm {
bool ScalarExpressionPlan::repeatCalculations(const Entry& entry,
    ExecutionSlotBuffer& locals,
    int* scratch,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    assert(entry.calculationLoop && entry.terminal == Terminal::Store && entry.nextPc == pc);
    const auto integer = [&](int slot) {
        const auto& local = locals[static_cast<std::size_t>(slot)];
        if (const auto* scalar = local.inlineScalar())
            return scalar->ptr<int>() != nullptr;
        const auto* value = local.boxedCarrier();
        return value && exactValuePayload<int>(*value);
    };
    for (int i = entry.offset; i < entry.offset + entry.count; ++i) {
        const auto& node = nodes[static_cast<std::size_t>(i)];
        if (node.operation == Operation::Local && !integer(node.operand))
            return false;
    }
    const auto& condition = entries[static_cast<std::size_t>(entry.loopCondition)];
    for (int i = condition.offset; i < condition.offset + condition.count; ++i) {
        const auto& node = nodes[static_cast<std::size_t>(i)];
        if (node.operation == Operation::Local && !integer(node.operand))
            return false;
    }
    int* destination = inlineInteger(locals, entry.destination);
    int value = 0;
    BoundLoop loop(*this, entry, locals, &value);
    if (!destination || !loop.valid())
        return false;
    // Read real locals and execute every checked operation on every iteration.
    // Only owned primitive reads survive admission; no host boundary can occur.
    return loop.run(
        remaining,
        pc,
        lastPc,
        [&] {
            return evaluateCalculations(entry, locals, scratch, value);
        },
        [&] {
            *destination = value;
            return true;
        });
}

bool ScalarExpressionPlan::executeConstructionOperands(const Entry& entry,
    const ExecutionSlotBuffer& locals,
    ExecutionSlotBuffer& stack,
    int* scratch,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    assert((entry.terminal == Terminal::ConstructionOperands
               || entry.terminal == Terminal::ConstructionLoop)
        && entry.count == 0 && entry.destination > 0);
    if ((remaining >= 0 && remaining < entry.cost)
        || stack.capacity() - stack.size() < static_cast<std::size_t>(entry.destination))
        return false;
    // No effects before every actual payload and checked operation qualifies.
    if (!evaluateCalculationResults(entry, locals, scratch))
        return false;
    for (int i = 0; i != entry.destination; ++i) {
        const auto& root = nodes[entry.offset + i];
        if (root.operation == Operation::Local && !locals[root.operand].scalarPointer<int>())
            return false;
    }
    for (int i = 0; i != entry.destination; ++i) {
        const auto& root = nodes[entry.offset + i];
        if (root.operation == Operation::Local)
            stack.push_back(locals[root.operand]);
        else {
            assert(root.operation == Operation::Constant || root.operation == Operation::Temporary);
            stack.emplace_back(ScriptScalar(
                root.operation == Operation::Constant ? root.operand : scratch[root.operand]));
        }
    }
    if (remaining > 0)
        remaining -= entry.cost;
    lastPc = entry.lastPc;
    pc = entry.nextPc;
    return true;
}

bool ScalarExpressionPlan::repeatConstruction(const Entry& entry,
    ExecutionSlotBuffer& locals,
    ExecutionSlotBuffer& stack,
    int* scratch,
    PrivateDictionaryWriteScratch& targets,
    std::size_t limit,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    assert(entry.terminal == Terminal::ConstructionLoop && entry.rangeBody >= 0);
    if (core::currentCpuMemoryQuota())
        return false;
    const int start = pc;
    const auto& tail = entries[static_cast<std::size_t>(entry.rangeBody)];
    const int container = entry.conditionalTail;
    const bool dictionary = entry.conditionalBranch == 1;
    int* destination = inlineInteger(locals, tail.destination);
    int* stepDestination = inlineInteger(locals, tail.stepLocal);
    int value = 0;
    BoundLoop loop(*this, tail, locals, &value);
    if (!destination || !stepDestination || !loop.valid())
        return false;
    if (!executeConstructionOperands(entry, locals, stack, scratch, remaining, pc, lastPc))
        return false;
    const auto affordable = [&](int cost) {
        return remaining < 0 || remaining >= cost;
    };
    // Producer effects already committed. Any later miss replays its current
    // original PC, never the producer or an earlier container replacement.
    if (!affordable(2))
        return true;
    ScriptSemanticRuntime::PrivateConstructionBorrow borrow;
    // Reuse the carrier-aware index borrow: imported indices keep their own
    // boxed Value, while literal/computed indices use the original integer cache.
    ScriptSemanticRuntime::IntegerIndexBorrow indices;
    if (!indices.bind(locals[container]))
        return true;
    if (!borrow.bind(locals[container],
            stack,
            entry.destination / (dictionary ? 2 : 1),
            limit,
            dictionary,
            targets))
        return true;
    const auto read = [&](const Node& node, int& result) {
        if (node.operation != Operation::Index)
            return operand(node, locals, result);
        const Value* mapped = nullptr;
        if (node.index.kind == Node::IndexKind::Local)
            mapped = indices.read(locals[node.index.operand]);
        else {
            int index = node.index.operand;
            if (node.index.kind == Node::IndexKind::LocalRemainder) {
                const int* actual = locals[node.index.operand].scalarPointer<int>();
                if (!actual)
                    return false;
                const auto remainder =
                    ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Mod>(
                        *actual, node.divisor);
                if (!remainder.ok())
                    return false;
                index = static_cast<int>(remainder.value.integer());
            }
            mapped = indices.read(ExecutionSlot(ScriptScalar(index)));
        }
        const int* payload = mapped ? checkedOwnedValuePayload<int>(*mapped) : nullptr;
        if (!payload)
            return false;
        result = *payload;
        return true;
    };
    do {
        if (!affordable(2) || !borrow.commit(stack))
            break;
        if (remaining > 0)
            remaining -= 2;
        pc = entry.rangeBody;
        lastPc = pc - 1;
        if (!affordable(tail.cost) || !evaluateCalculationResultsWithReader(tail, scratch, read))
            break;
        value = scratch[tail.calculationCount - 1];
        int step;
        bool repeat;
        if (!loop.prepare(step, repeat))
            break;
        *destination = value;
        *stepDestination = step;
        if (remaining > 0)
            remaining -= tail.cost;
        lastPc = tail.lastPc;
        pc = repeat ? tail.nextPc
                    : entries[static_cast<std::size_t>(tail.loopCondition)].destination;
        if (pc != start)
            break;
        if (!executeConstructionOperands(entry, locals, stack, scratch, remaining, pc, lastPc))
            break;
    } while (true);
    return true;
}

namespace {
template <class T>
auto payloadComparator(ScalarExpressionPlan::Operation operation) -> bool (*)(const T&, const T&)
{
    using Operation = ScalarExpressionPlan::Operation;
    switch (operation) {
    case Operation::Equal:
        return &ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Equal, T>;
    case Operation::NotEqual:
        return &ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::NotEqual, T>;
    case Operation::Less:
        return &ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Less, T>;
    case Operation::LessEqual:
        return &ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::LessEqual, T>;
    case Operation::Greater:
        return &ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::Greater, T>;
    case Operation::GreaterEqual:
        return &ScriptSemanticRuntime::comparePayload<SemanticCompareOperation::GreaterEqual, T>;
    default:
        return nullptr;
    }
}
} // namespace

ScalarExpressionPlan::GuardedReturn ScalarExpressionPlan::GuardedReturn::prepare(
    const Function& function, const ScalarExpressionPlan& plan)
{
    GuardedReturn result;
    if (function.isGenerator || function.isVariadic || function.captureCount != 0
        || !function.safepoints.empty() || !function.exceptionRegions.empty()
        || plan.entries.empty())
        return result;
    const auto& branch = plan.entries[0];
    if (branch.count != 3 || branch.floating || branch.leafCost != 0 || branch.leafFrameValues != 0
        || branch.stepLocal >= 0 || branch.loopCondition >= 0
        || (branch.terminal != Terminal::BranchFalse && branch.terminal != Terminal::BranchTrue)
        || branch.nextPc < 0 || branch.nextPc + 1 >= static_cast<int>(function.code.size())
        || branch.destination <= branch.lastPc)
        return result;
    const auto& returned = function.code[branch.nextPc];
    const auto& terminal = function.code[branch.nextPc + 1];
    if (terminal.op != Opcode::ReturnInt && !(terminal.op == Opcode::Return && terminal.a == 1))
        return result;
    if (returned.op == Opcode::LoadLocal && returned.a >= 0 && returned.a < function.params)
        result.resultLocal_ = returned.a;
    else if (returned.op == Opcode::LoadInt && returned.i64 >= std::numeric_limits<int>::min()
        && returned.i64 <= std::numeric_limits<int>::max())
        result.resultConstant_ = static_cast<int>(returned.i64);
    else
        return {};
    const auto* nodes = plan.nodes.data() + branch.offset;
    const auto operand = [&](const Node& node, int& local, int& constant) {
        if (node.operation == Operation::Constant) {
            constant = node.operand;
            return true;
        }
        if (node.operation != Operation::Local || node.operand < 0
            || node.operand >= function.params)
            return false;
        local = node.operand;
        return true;
    };
    if (!operand(nodes[0], result.leftLocal_, result.leftConstant_)
        || !operand(nodes[1], result.rightLocal_, result.rightConstant_))
        return {};
    result.compare_ = payloadComparator<int>(nodes[2].operation);
    result.returnWhenTrue_ = branch.terminal == Terminal::BranchFalse;
    result.prefixCost_ = branch.cost;
    result.resumePc_ = branch.destination;
    result.lastPc_ = branch.lastPc;
    return result;
}

bool ScalarExpressionPlan::executeIndependent(const Entry& entry,
    ExecutionSlotBuffer& locals,
    ExecutionSlotBuffer& stack,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    assert(independentStackSpans && entry.count > 0 && entry.evaluator && !entry.floating
        && entry.leafCost == 0 && entry.stepLocal < 0 && entry.loopCondition < 0);
    assert(entry.terminal == Terminal::Store || entry.terminal == Terminal::Push
        || entry.terminal == Terminal::BranchFalse || entry.terminal == Terminal::BranchTrue);
    if (remaining >= 0 && remaining < entry.cost)
        return false;
    int value;
    if (!evaluateSlots(entry, locals, nullptr, value))
        return false;
    if (remaining > 0)
        remaining -= entry.cost;
    lastPc = entry.lastPc;
    pc = entry.nextPc;
    switch (entry.terminal) {
    case Terminal::Store:
        locals[static_cast<std::size_t>(entry.destination)] = ScriptScalar(value);
        break;
    case Terminal::Push:
        stack.push_back(ExecutionSlot(ScriptScalar(value)));
        break;
    case Terminal::BranchFalse:
        if (value == 0)
            pc = entry.destination;
        break;
    case Terminal::BranchTrue:
        if (value != 0)
            pc = entry.destination;
        break;
    default:
        assert(false);
        break;
    }
    return true;
}

bool ScalarExpressionPlan::repeatConditional(const Entry& entry,
    ExecutionSlotBuffer& locals,
    std::int64_t& remaining,
    int& pc,
    int& lastPc) const
{
    const bool floating = entry.conditionalBranch >= 0;
    const auto& branch =
        floating ? entries[static_cast<std::size_t>(entry.conditionalBranch)] : entry;
    const auto& body = entries[static_cast<std::size_t>(entry.conditionalBody)];
    const auto& tail = entries[static_cast<std::size_t>(entry.conditionalTail)];
    auto joined = body;
    joined.stepLocal = tail.destination;
    joined.stepConstant = nodes[tail.offset + 1].operand;
    joined.subtractStep = nodes[tail.offset + 2].operation == Operation::Sub;
    joined.loopCondition = tail.loopCondition;
    joined.nextPc = tail.nextPc;
    joined.lastPc = tail.lastPc;
    joined.cost = entry.cost + tail.cost + (floating ? branch.cost : 0);
    if (remaining >= 0 && remaining < joined.cost)
        return false;
    int* destination = inlineInteger(locals, body.destination);
    int value = 0;
    BoundLoop loop(*this, joined, locals, &value);
    if (!destination || !loop.valid())
        return false;
    const auto* expression = nodes.data() + branch.offset;
    IntegerIndexAccess index;
    std::string_view localText;
    const bool indexed = expression[0].operation == Operation::Index;
    double* floatDestination = nullptr;
    double floatingValue = 0.0;
    if (floating) {
        const auto* scalar = locals[static_cast<std::size_t>(entry.destination)].inlineScalar();
        if (!scalar || !(floatDestination = const_cast<double*>(scalar->ptr<double>())))
            return false;
    } else if (indexed ? !index.bindStrings(expression[0], locals)
                       : !stringOperand(expression[0], locals, localText))
        return false;
    const auto compareText =
        floating ? nullptr : payloadComparator<std::string_view>(expression[2].operation);
    const auto compareFloat =
        floating ? payloadComparator<double>(expression[2].operation) : nullptr;
    assert(floating ? compareFloat != nullptr : compareText != nullptr);
    const std::string_view literal = floating ? std::string_view{} : *expression[1].string;
    bool taken = false;
    return loop.run(
        remaining,
        pc,
        lastPc,
        [&] {
            if (floating) {
                if (!evaluateFloatSlots(entry, locals, nullptr, floatingValue))
                    return false;
                taken = compareFloat(floatingValue, expression[1].floating);
            } else {
                auto text = localText;
                if (indexed) {
                    const auto* payload = index.readString();
                    if (!payload)
                        return false;
                    text = *payload;
                }
                taken = compareText(text, literal);
            }
            value = *destination;
            return !taken || evaluateSlots(body, locals, nullptr, value);
        },
        [&] {
            if (floating)
                *floatDestination = floatingValue;
            if (taken)
                *destination = value;
            return true;
        },
        [&] {
            return joined.cost + (taken ? body.cost : 0);
        });
}

template <ScalarExpressionPlan::Operation Op>
bool ScalarExpressionPlan::boundDivisionOperand(
    int lhs, const Node& node, const BoundIntSlots& locals, int& value)
{
    int rhs;
    return operand<int, true>(node, locals, rhs) && applyConstant<Op>(lhs, rhs, node, value);
}

template bool ScalarExpressionPlan::boundDivisionOperand<ScalarExpressionPlan::Operation::Div>(
    int, const Node&, const BoundIntSlots&, int&);
template bool ScalarExpressionPlan::boundDivisionOperand<ScalarExpressionPlan::Operation::Mod>(
    int, const Node&, const BoundIntSlots&, int&);

} // namespace arp::scriptvm

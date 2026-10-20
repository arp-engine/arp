#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/semantic_runtime.h"

#include <cstring>

namespace arp::scriptvm {

bool Vm::executeScalarMethodLoop(const Program& program,
    Frame& frame,
    const ScalarExpressionPlan& plan,
    const ScalarExpressionPlan::Entry& entry,
    std::int64_t& remaining)
{
    assert(entry.terminal == ScalarExpressionPlan::Terminal::MethodLoop && entry.rangeBody >= 0);
    if (memoryQuota_ || core::currentCpuMemoryQuota() || callBridge_ || !frame.handlers.empty())
        return false;
    const auto& node = plan.nodes[static_cast<std::size_t>(entry.rangeBody)];
    const auto* receiver =
        frame.locals[static_cast<std::size_t>(node.call.targetLocal)].boxedCarrier();
    if (!receiver || !exactValuePayload<arp::reflection::instance>(*receiver))
        return false;
    const auto& instruction = frame.function->code[static_cast<std::size_t>(node.call.sourcePc)];
    const int index = resolveScriptMethod(program, instruction, *receiver);
    if (index < 0 || index >= static_cast<int>(program.functions.size()))
        return false;
    const auto& callee = program.functions[static_cast<std::size_t>(index)];
    const auto* leaf = validatedProgram_.scalarExpressionsOwned(&callee);
    if (!leaf || !leaf->argumentLeaf || callee.params != 2 || callee.code[0].a != 1)
        return false;
    const int leafCost = leaf->entries[0].cost;
    if ((remaining >= 0 && remaining < entry.cost + leafCost)
        || !admitsScalarArgumentLeaf(callee, leafCost, 1, true, remaining))
        return false;
    // This exact closed interval writes only its scalar destination and step.
    // All other payloads are real owned integers, so reads/destructors cannot
    // call host code and invalidate the admitted receiver or method selection.
    if (!plan.admitsClosedLeafLocals(entry, frame.locals))
        return false;
    if (frame.integerScratch.size() < plan.scratchSize)
        frame.integerScratch.resize(plan.scratchSize);
    return plan.repeatCall(entry,
        frame.locals,
        frame.integerScratch.data(),
        leafCost,
        remaining,
        frame.ip,
        frame.lastIp,
        [&](int& value) {
            // Keep the original argument and callee carriers in their original
            // order. Release the argument while the logical leaf is active;
            // the bound receiver survives until return, before caller arithmetic.
            ExecutionSlot argument(frame.locals[static_cast<std::size_t>(node.operand)]);
            ExecutionSlot calleeSlot(ScriptMethod{index, *receiver});
            if (!admitsScalarArgumentLeaf(callee, leafCost, 1, true, remaining))
                return false;
            ScriptExecutionFrameScope logicalFrame(executionSession_.get());
            if (!logicalFrame.active() || !leaf->evaluateArgument(argument, value))
                return false;
            argument = ScriptScalar();
            return true;
        });
}

bool Vm::executeScalarCaptureLeaf(const Function& callee,
    const ScalarExpressionPlan::CaptureLeaf& leaf,
    const ScriptClosure& closure,
    Frame& caller,
    std::size_t argumentCount,
    std::int64_t& remaining)
{
    // All reads and checked arithmetic precede any effect. The original callee
    // and its precise PC/traceback replay on every guard/overflow/budget miss.
    // Keep the original indirect-call argument buffer and its ownership. It
    // releases prior carriers before reading captures and retains current ones
    // until the next collection, including unused host-owned arguments.
    return evaluateScalarCaptureLeaf(
        callee, leaf, closure, argumentCount, remaining, [&](int result) {
            if (remaining > 0)
                remaining -= leaf.cost;
            caller.push(ScriptScalar(result));
        });
}

bool Vm::executeScalarClosureLoop(const Program& program,
    Frame& frame,
    const ScalarExpressionPlan& plan,
    const ScalarExpressionPlan::Entry& entry,
    std::int64_t& remaining)
{
    assert(plan.independentStackSpans
        && entry.terminal == ScalarExpressionPlan::Terminal::ClosureLoop);
    // This qualification is local to an empty-stack closed interval. It does
    // not assume that a dynamic indirect caller has a whole-function proof.
    if (memoryQuota_ || core::currentCpuMemoryQuota() || callBridge_ || !frame.stack.empty()
        || !frame.handlers.empty())
        return false;
    const auto limit =
        std::min(maxFrameValues_, static_cast<std::size_t>(frame.function->maxStack));
    if (plan.scratchSize > limit)
        return false;
    const auto& node = plan.nodes[static_cast<std::size_t>(entry.rangeBody)];
    const auto* carrier =
        frame.locals[static_cast<std::size_t>(node.call.targetLocal)].boxedCarrier();
    const auto* closure = carrier ? exactValuePayload<ScriptClosure>(*carrier) : nullptr;
    if (!closure || closure->aotModule || (closure->vmOwner && closure->vmOwner.get() != this)
        || closure->funcIndex < 0
        || closure->funcIndex >= static_cast<int>(program.functions.size()))
        return false;
    const auto& callee = program.functions[static_cast<std::size_t>(closure->funcIndex)];
    const auto* leafPlan = validatedProgram_.scalarExpressionsOwned(&callee);
    if (!leafPlan || !leafPlan->captureLeaf.valid() || callee.params != 1
        || leafPlan->captureLeaf.argument != 0)
        return false;
    const auto& leaf = leafPlan->captureLeaf;
    if ((remaining >= 0 && remaining < entry.cost + leaf.cost)
        || !admitsScalarCaptureLeaf(callee, leaf, *closure, 1, remaining)
        || !exactValuePayload<int>(closure->captures[static_cast<std::size_t>(leaf.capture)]->value)
        || !plan.admitsClosedLeafLocals(entry, frame.locals)
        || !std::all_of(callSlots_.begin(), callSlots_.end(), ScalarExpressionPlan::ownedInteger))
        return false;
    // Old argument carriers can run arbitrary destructors. Admit only actual
    // primitive old slots; all other cases perform their original collection
    // first. Current arguments still live in the original per-VM buffer.
    if (frame.integerScratch.size() < plan.scratchSize)
        frame.integerScratch.resize(plan.scratchSize);
    return plan.repeatCall(entry,
        frame.locals,
        frame.integerScratch.data(),
        leaf.cost,
        remaining,
        frame.ip,
        frame.lastIp,
        [&](int& value) {
            ExecutionSlot argument(frame.locals[static_cast<std::size_t>(node.operand)]);
            ExecutionSlot calleeSlot(*carrier);
            callSlots_.clear();
            callSlots_.reserve(1);
            callSlots_.push_back(std::move(argument));
            return evaluateScalarCaptureLeaf(callee, leaf, *closure, 1, remaining, [&](int result) {
                value = result;
            });
        });
}

bool Vm::executeScalarArgumentLeaf(const Function& callee,
    const ScalarExpressionPlan& leaf,
    Frame& caller,
    std::size_t argumentCount,
    bool hasReceiver,
    std::int64_t& remaining)
{
    assert(leaf.argumentLeaf
        && static_cast<std::size_t>(callee.params) == argumentCount + hasReceiver);
    assert(argumentCount <= caller.stack.size());
    const auto& entry = leaf.entries[0];
    const int argument = callee.code[0].a - static_cast<int>(hasReceiver);
    // A bound receiver is kept by the original callee slot. This path reads
    // only an actual script argument; receiver reads retain normal execution.
    // New shapes keep frame allocation/admission order under an active quota.
    if (argument < 0
        || !admitsScalarArgumentLeaf(callee, entry.cost, argumentCount, hasReceiver, remaining))
        return false;
    ScriptExecutionFrameScope logicalFrame(executionSession_.get());
    if (!logicalFrame.active())
        return false;
    const auto prefix = caller.stack.size() - argumentCount;
    int result;
    if (!leaf.evaluateArgument(caller.stack[prefix + static_cast<std::size_t>(argument)], result))
        return false;
    // No effect precedes success. Failed arithmetic or a short budget replays
    // the complete original callee, including its PC/traceback. Unused slots
    // remain alive through evaluation and release in parameter order while
    // the logical callee scope is active, before publishing its return value.
    if (remaining > 0)
        remaining -= entry.cost;
    caller.stack.resize(prefix);
    caller.push(ScriptScalar(result));
    return true;
}

// A small dispatcher for independently stack-proved scalar control flow.
// It executes the original bytecode and shares Frame, checked
// semantics, expression handlers and call admission with the full interpreter.
// A payload/budget guard leaves the current instruction untouched for replay.
Vm::ScalarDispatchStatus Vm::executeScalarDispatch(const Program& program,
    Frame& frame,
    VmError& error,
    Value* result,
    bool* produced,
    ExecutionSlot* slotResult)
{
    assert(result != nullptr || slotResult != nullptr);
    auto& budget =
        executionSession_ ? executionSession_->remainingInstructions : remainingInstructions_;
    const auto* plan = validatedProgram_.scalarExpressionsOwned(frame.function);
    const auto& code = frame.function->code;
    assert(frame.captures.empty());
    const auto commit = [&](int pc) {
        if (budget > 0)
            --budget;
        frame.lastIp = pc;
        frame.ip = pc + 1;
    };
    const auto finish = [&](ExecutionSlot&& slot) {
        if (slotResult != nullptr && !frame.replaceReturnValue)
            *slotResult = std::move(slot);
        else {
            if (result != nullptr) {
                *result = frame.replaceReturnValue ? frame.returnValue.boxed() : slot.takeValue();
                if (slotResult != nullptr)
                    *slotResult = ExecutionSlot(*result);
            } else
                *slotResult = ExecutionSlot(frame.returnValue);
        }
        if (produced != nullptr)
            *produced = true;
    };
    const auto binary = [&](auto operation, bool typed) {
        assert(frame.stack.size() >= 2);
        ScriptScalar lhs, rhs;
        if (!frame.peekScalarPair<true>(lhs, rhs))
            return false;
        if (typed
            && (lhs.kind() != ScriptScalar::Kind::Int || rhs.kind() != ScriptScalar::Kind::Int))
            return false;
        ScalarOperationResult value;
        if constexpr (decltype(operation)::value == SemanticBinaryOperation::Add
            || decltype(operation)::value == SemanticBinaryOperation::Sub
            || decltype(operation)::value == SemanticBinaryOperation::Mul
            || decltype(operation)::value == SemanticBinaryOperation::Div
            || decltype(operation)::value == SemanticBinaryOperation::Mod) {
            if (typed)
                value = ScriptSemanticRuntime::intBinary<decltype(operation)::value>(
                    static_cast<int>(lhs.integer()), static_cast<int>(rhs.integer()));
            else if (!ScriptSemanticRuntime::scalarBinary<decltype(operation)::value>(
                         lhs, rhs, value))
                return false;
        } else {
            assert(!typed);
            if (!ScriptSemanticRuntime::scalarBinary(decltype(operation)::value, lhs, rhs, value))
                return false;
        }
        if (!value.ok())
            return false;
        frame.replaceScalarPair<true>(value.value);
        return true;
    };
    while (frame.ip < static_cast<int>(code.size())) {
        if (budget == 0)
            return ScalarDispatchStatus::Replay;
        const int pc = frame.ip;
        if (plan != nullptr) {
            const auto& entry = plan->entries[static_cast<std::size_t>(pc)];
            if (entry.calculationLoop && memoryQuota_ == nullptr
                && (budget < 0 || budget >= entry.cost)) {
                if (frame.integerScratch.size() < plan->scratchSize)
                    frame.integerScratch.resize(plan->scratchSize);
                if (plan->repeatCalculations(entry,
                        frame.locals,
                        frame.integerScratch.data(),
                        budget,
                        frame.ip,
                        frame.lastIp))
                    continue;
            }
            if (entry.boundIntEvaluator
                && plan->repeatInteger(entry, frame.locals, budget, frame.ip, frame.lastIp))
                continue;
            if (entry.conditionalBody >= 0
                && plan->repeatConditional(entry, frame.locals, budget, frame.ip, frame.lastIp))
                continue;
            if (entry.count != 0 && entry.leafFrameValues == 0
                && (budget < 0 || budget >= entry.cost)) {
                int truth = 0;
                int stepValue = 0;
                bool repeat = true;
                ScriptScalar value;
                const auto evaluate = [&] {
                    if (entry.floating) {
                        double number;
                        if (!entry.floatEvaluator && frame.floatScratch.size() < plan->scratchSize)
                            frame.floatScratch.resize(plan->scratchSize);
                        if (!plan->evaluateFloatSlots(
                                entry, frame.locals, frame.floatScratch.data(), number))
                            return false;
                        truth = number != 0.0;
                        value = ScriptScalar(number);
                    } else {
                        int number;
                        if (!entry.evaluator && frame.integerScratch.size() < plan->scratchSize)
                            frame.integerScratch.resize(plan->scratchSize);
                        if (!plan->evaluateSlots(
                                entry, frame.locals, frame.integerScratch.data(), number))
                            return false;
                        truth = number != 0;
                        value = ScriptScalar(number);
                    }
                    return true;
                };
                if (evaluate()
                    && plan->prepareTail(entry, frame.locals, value, stepValue, repeat)) {
                    if (budget > 0)
                        budget -= entry.cost;
                    frame.lastIp = entry.lastPc;
                    frame.ip = entry.loopCondition >= 0 && !repeat
                        ? plan->entries[static_cast<std::size_t>(entry.loopCondition)].destination
                        : entry.nextPc;
                    switch (entry.terminal) {
                    case ScalarExpressionPlan::Terminal::Push:
                        frame.push(value);
                        break;
                    case ScalarExpressionPlan::Terminal::Store:
                        frame.locals[static_cast<std::size_t>(entry.destination)] = value;
                        if (entry.stepLocal >= 0)
                            frame.locals[static_cast<std::size_t>(entry.stepLocal)] =
                                ScriptScalar(stepValue);
                        break;
                    case ScalarExpressionPlan::Terminal::BranchFalse:
                        if (!truth)
                            frame.ip = entry.destination;
                        break;
                    case ScalarExpressionPlan::Terminal::BranchTrue:
                        if (truth)
                            frame.ip = entry.destination;
                        break;
                    case ScalarExpressionPlan::Terminal::Return:
                        finish(ExecutionSlot(value));
                        return ScalarDispatchStatus::Returned;
                    default:
                        assert(false);
                        return ScalarDispatchStatus::Replay;
                    }
                    continue;
                }
            }
        }
        const auto& instruction = code[static_cast<std::size_t>(pc)];
        switch (instruction.op) {
        case Opcode::LoadInt:
            frame.push(ScriptScalar(static_cast<int>(instruction.i64)));
            break;
        case Opcode::LoadFloat: {
            double value;
            static_assert(sizeof(value) == sizeof(instruction.i64));
            std::memcpy(&value, &instruction.i64, sizeof(value));
            frame.push(ScriptScalar(value));
            break;
        }
        case Opcode::LoadLocal:
            frame.push(frame.locals[static_cast<std::size_t>(instruction.a)]);
            break;
        case Opcode::StoreLocal:
            frame.locals[static_cast<std::size_t>(instruction.a)] = frame.popSlot<true>();
            break;
        case Opcode::Pop:
            frame.popSlot<true>();
            break;
        case Opcode::LocalAddIntConst:
        case Opcode::LocalSubIntConst: {
            auto& local = frame.locals[static_cast<std::size_t>(instruction.a)];
            const int* current = local.scalarPointer<int>();
            if (!current)
                return ScalarDispatchStatus::Replay;
            const auto value = instruction.op == Opcode::LocalAddIntConst
                ? ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Add>(
                      *current, static_cast<int>(instruction.i64))
                : ScriptSemanticRuntime::intBinary<SemanticBinaryOperation::Sub>(
                      *current, static_cast<int>(instruction.i64));
            if (!value.ok())
                return ScalarDispatchStatus::Replay;
            local = value.value;
            break;
        }
        case Opcode::Jump:
            commit(pc);
            frame.ip = instruction.a;
            continue;
        case Opcode::JumpIfLocalIntLtConstFalse: {
            const int* local =
                frame.locals[static_cast<std::size_t>(instruction.b)].scalarPointer<int>();
            if (!local)
                return ScalarDispatchStatus::Replay;
            const bool taken = *local >= static_cast<int>(instruction.i64);
            commit(pc);
            if (taken)
                frame.ip = instruction.a;
            continue;
        }
        case Opcode::JumpIfFalse:
        case Opcode::JumpIfTrue: {
            assert(!frame.stack.empty());
            const bool* condition = frame.stack.back().scalarPointer<bool>();
            if (!condition)
                return ScalarDispatchStatus::Replay;
            const bool taken = *condition == (instruction.op == Opcode::JumpIfTrue);
            frame.popSlot<true>();
            commit(pc);
            if (taken)
                frame.ip = instruction.a;
            continue;
        }
#define ARP_SCALAR_CALL_BINARY(Name)                                                               \
    case Opcode::Int##Name:                                                                        \
    case Opcode::Name:                                                                             \
        if (!binary(                                                                               \
                std::integral_constant<SemanticBinaryOperation, SemanticBinaryOperation::Name>{},  \
                instruction.op == Opcode::Int##Name))                                              \
            return ScalarDispatchStatus::Replay;                                                   \
        break
            ARP_SCALAR_CALL_BINARY(Add);
            ARP_SCALAR_CALL_BINARY(Sub);
            ARP_SCALAR_CALL_BINARY(Mul);
            ARP_SCALAR_CALL_BINARY(Div);
            ARP_SCALAR_CALL_BINARY(Mod);
#undef ARP_SCALAR_CALL_BINARY
#define ARP_SCALAR_EXTENDED_BINARY(Name)                                                           \
    case Opcode::Name:                                                                             \
        if (!binary(                                                                               \
                std::integral_constant<SemanticBinaryOperation, SemanticBinaryOperation::Name>{},  \
                false))                                                                            \
            return ScalarDispatchStatus::Replay;                                                   \
        break
            ARP_SCALAR_EXTENDED_BINARY(Pow);
            ARP_SCALAR_EXTENDED_BINARY(FloorDiv);
            ARP_SCALAR_EXTENDED_BINARY(BitAnd);
            ARP_SCALAR_EXTENDED_BINARY(BitOr);
            ARP_SCALAR_EXTENDED_BINARY(BitXor);
            ARP_SCALAR_EXTENDED_BINARY(BitClear);
            ARP_SCALAR_EXTENDED_BINARY(ShiftLeft);
            ARP_SCALAR_EXTENDED_BINARY(ShiftRight);
#undef ARP_SCALAR_EXTENDED_BINARY
        case Opcode::Call: {
            // Runtime limits remain mutable even for immutable bytecode.
            if (callBridge_ != nullptr
                || static_cast<std::size_t>(instruction.b) > maxCallArguments_)
                return ScalarDispatchStatus::Replay;
            assert(instruction.b >= 0
                && static_cast<std::size_t>(instruction.b) <= frame.stack.size());
            commit(pc);
            ExecutionSlot returned;
            bool callProduced = false;
            const ExecutionArgumentsView arguments(
                frame.stack, static_cast<std::size_t>(instruction.b));
            runStaticFunctionSlots(program,
                instruction.a,
                arguments,
                frame.captures,
                &error,
                &callProduced,
                &returned);
            arguments.finishConsumption();
            if (error.code != ScriptDiagnosticCode::None)
                return ScalarDispatchStatus::Failed;
            assert(callProduced);
            frame.push(std::move(returned));
            if (frame.stack.size() > maxFrameValues_) {
                error.code = ScriptDiagnosticCode::ResourceLimitExceeded;
                error.message = "script frame values exceed the execution limit";
                return ScalarDispatchStatus::Failed;
            }
            // A callee that falls back to dynamic semantics may have crossed
            // a host boundary and changed this VM's mutable limits.
            if (static_cast<std::size_t>(frame.function->maxStack) > maxFrameValues_)
                return ScalarDispatchStatus::Replay;
            continue;
        }
        case Opcode::ReturnInt:
        case Opcode::Return:
            commit(pc);
            finish(frame.popSlot<true>());
            return ScalarDispatchStatus::Returned;
        default:
            return ScalarDispatchStatus::Replay;
        }
        commit(pc);
    }
    return ScalarDispatchStatus::Replay;
}

} // namespace arp::scriptvm

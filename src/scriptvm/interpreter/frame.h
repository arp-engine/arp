#pragma once

// ArpScript: VM call frame.
//
// Stack-based VM with local variable slots (same hybrid model as goes).
// Each frame owns:
//   - locals: inline scalar/reference slots (params at 0..params-1)
//   - captures: Vector<SharedPtr<ScriptCell>> for closure captures
//   - stack: inline scalar/reference operand slots; Value at ABI boundaries
//
// Capture cells are shared_ptr so multiple closures capturing the same
// outer variable share one cell (upvalue sharing, like goes/Lua).

#include "core/cpu_allocator.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/runtime/execution_slot.h"
#include "scriptvm/runtime/value.h"

#include <cassert>
#include <vector>

namespace arp::scriptvm {

class Vm;

struct Frame {
    struct HandlerInfo {
        int handlerPc = 0;
        std::size_t stackDepth = 0;
    };

    const Function* function = nullptr;
    int ip = 0;     // instruction pointer
    int lastIp = 0; // last executed instruction (for traceback)
    ExecutionSlotBuffer locals{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> captures{
        &arp::core::defaultCpuMemoryResource()};
    ExecutionSlotBuffer stack{&arp::core::defaultCpuMemoryResource()};
    // Ephemeral evaluation workspace, not continuation/Value state. A pooled
    // frame retains capacity; copied/adopted frames may start with it empty.
    core::pmr::Vector<int> integerScratch{&arp::core::defaultCpuMemoryResource()};
    core::pmr::Vector<double> floatScratch{&arp::core::defaultCpuMemoryResource()};
    PrivateDictionaryWriteScratch dictionaryScratch{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<HandlerInfo> handlers{&arp::core::defaultCpuMemoryResource()};
    // MakeInstance may need a script constructor's internal return to be
    // replaced by its preallocated receiver. This travels with a suspended
    // constructor frame so resumption and backend adoption preserve it.
    bool replaceReturnValue = false;
    // Most frames never replace their return. Keep that empty state inline;
    // a real receiver/imported override retains its exact Value carrier.
    ExecutionSlot returnValue;
    // Lazy per-slot cell cache: when a local is captured (AddrLocal), the
    // current value is moved into a shared ScriptCell and the slot is
    // replaced with a ScriptPointer to that cell. Subsequent AddrLocal on
    // the same slot reuses the cell so multiple closures share one upvalue.
    // (Go-style upvalue hoisting would migrate the cell into captures; here
    //  we keep it on the stack for simplicity -- the slot holds a Pointer.)

    Frame() = default;

    Frame(const Frame& other)
        : function(other.function), ip(other.ip), lastIp(other.lastIp),
          locals(other.locals, &arp::core::defaultCpuMemoryResource()),
          captures(other.captures, &arp::core::defaultCpuMemoryResource()),
          stack(other.stack, &arp::core::defaultCpuMemoryResource()),
          handlers(other.handlers, &arp::core::defaultCpuMemoryResource()),
          replaceReturnValue(other.replaceReturnValue), returnValue(other.returnValue)
    {}

    Frame(Frame&& other) noexcept
        : function(other.function), ip(other.ip), lastIp(other.lastIp),
          locals(std::move(other.locals), &arp::core::defaultCpuMemoryResource()),
          captures(std::move(other.captures), &arp::core::defaultCpuMemoryResource()),
          stack(std::move(other.stack), &arp::core::defaultCpuMemoryResource()),
          handlers(std::move(other.handlers), &arp::core::defaultCpuMemoryResource()),
          replaceReturnValue(other.replaceReturnValue), returnValue(std::move(other.returnValue))
    {}

    Frame& operator=(const Frame& other)
    {
        if (this == &other)
            return *this;
        function = other.function;
        ip = other.ip;
        lastIp = other.lastIp;
        locals = other.locals;
        captures = other.captures;
        stack = other.stack;
        handlers = other.handlers;
        replaceReturnValue = other.replaceReturnValue;
        returnValue = other.returnValue;
        return *this;
    }

    Frame& operator=(Frame&& other) noexcept
    {
        if (this == &other)
            return *this;
        function = other.function;
        ip = other.ip;
        lastIp = other.lastIp;
        locals = std::move(other.locals);
        captures = std::move(other.captures);
        stack = std::move(other.stack);
        handlers = std::move(other.handlers);
        replaceReturnValue = other.replaceReturnValue;
        returnValue = std::move(other.returnValue);
        return *this;
    }

    explicit Frame(const Function* fn)
        : function(fn),
          locals(fn ? fn->locals : 0, ExecutionSlot(), &arp::core::defaultCpuMemoryResource()),
          stack(fn ? static_cast<std::size_t>(fn->maxStack) : 0,
              ExecutionSlot(),
              &arp::core::defaultCpuMemoryResource())
    {
        // stack logical size starts at 0; capacity = maxStack.
        stack.clear();
    }

    void reset(const Function* fn)
    {
        function = fn;
        ip = 0;
        lastIp = 0;
        locals.clear();
        stack.clear();
        captures.clear();
        handlers.clear();
        dictionaryScratch.clear();
        replaceReturnValue = false;
        returnValue = ScriptScalar();
        if (fn) {
            locals.resize(fn->locals);
            stack.reserve(fn->maxStack);
        }
    }

    void push(Value v) { stack.emplace_back(std::move(v)); }

    void push(const ExecutionSlot& slot) { stack.emplace_back(slot); }

    void push(ExecutionSlot&& slot) { stack.emplace_back(std::move(slot)); }

    void push(ScriptScalar scalar) { stack.emplace_back(scalar); }

    // Inspect operands in place. Only a successful scalar semantic operation
    // consumes them; dynamic operators retain the original reference carriers.
    template <bool StackProven = false>
    bool peekScalarPair(ScriptScalar& lhs, ScriptScalar& rhs) const
    {
        if constexpr (StackProven)
            assert(stack.size() >= 2u);
        else if (stack.size() < 2u)
            return false;
        return stack[stack.size() - 2u].scalar(lhs) && stack.back().scalar(rhs);
    }

    // Replace two already-read operands with an independent immutable result.
    // Replacing a boxed slot releases our carrier without mutating its aliases.
    template <bool StackProven = false> void replaceScalarPair(ScriptScalar result)
    {
        if constexpr (StackProven)
            assert(stack.size() >= 2u);
        else {
            assert(stack.size() >= 2u);
            if (stack.size() < 2u)
                return;
        }
        stack[stack.size() - 2u] = result;
        stack.pop_back();
    }

    template <bool StackProven = false> ExecutionSlot popSlot()
    {
        if constexpr (StackProven)
            assert(!stack.empty());
        else if (stack.empty())
            return {};
        auto slot = std::move(stack.back());
        stack.pop_back();
        return slot;
    }

    template <bool StackProven = false> Value pop(const IntegerValueCacheBorrow* cache = nullptr)
    {
        if constexpr (StackProven)
            assert(!stack.empty());
        else if (stack.empty())
            return Value::undefined();
        Value v = cache ? stack.back().takeValue(*cache) : stack.back().takeValue();
        stack.pop_back();
        return v;
    }

    Value& top() { return stack.back().boxed(); }

    const Value& top() const { return stack.back().boxed(); }

    std::size_t stackSize() const { return stack.size(); }

    // Peek at a value below the top (0 = top, 1 = below top, ...).
    Value& peekFromTop(std::size_t depth) { return stack[stack.size() - 1 - depth].boxed(); }
};

// A suspended script function. The Program is owned by the caller of the VM;
// the generator therefore remains valid only while that Program is alive.
struct ScriptGenerator {
    Frame frame;
    const Program* program = nullptr;
    std::shared_ptr<ScriptModuleRetentionLease> retentionLease;
    std::shared_ptr<const Program> programOwner;
    Vm* ownerVm = nullptr;
    std::shared_ptr<Vm> ownerVmOwner;
    core::pmr::String moduleId{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0;
    // Explicit cross-backend identity token supplied by the owning VM. An
    // Interpreter generator without this token cannot be adopted by a native
    // parent, even when module id/version happen to match.
    core::pmr::String bridgeArtifactIdentity{&arp::core::defaultCpuMemoryResource()};
    int funcIndex = -1;
    std::shared_ptr<Frame> suspendedFrame;
    Value suspendedAwaitable;
    // A backend may inject send()/throw() into a pending awaitable only when
    // the awaitable explicitly exposes the injection contract.  The result
    // is consumed once by resumeGenerator() without polling the awaitable a
    // second time, preserving the no-replay boundary.
    Value suspendedAwaitInjection;
    bool suspendedAwaitInjectionReady = false;
    // Per-frame program ownership for a bounded cross-module Interpreter
    // continuation. The root `program` remains the generator owner; these
    // hints identify the active/caller programs after validated adoption.
    const Program* suspendedFrameProgram = nullptr;
    std::shared_ptr<const Program> suspendedFrameProgramOwner;
    core::pmr::Vector<const Program*> suspendedCallerPrograms{
        &arp::core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<const Program>> suspendedCallerProgramOwners{
        &arp::core::defaultCpuMemoryResource()};
    Value yieldedValue;
    // Caller frames are ordered from the immediate caller outward. They are
    // populated when a generator suspends inside an ordinary callee.
    core::pmr::Vector<Frame> suspendedCallers{&arp::core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> suspendedCallerPushResults{
        &arp::core::defaultCpuMemoryResource()};
    bool resumingSuspendedCallers = false;
    // Bounded yield-from protocol state. The source slot and the last parent
    // Yield PC let send/close forward to a delegated child without guessing a
    // native instruction pointer.
    int delegatedSourceSlot = -1;
    int delegatedYieldPc = -1;
    bool delegatedActive = false;
    int delegatedPendingRangePc = -1;
    // Exact parent Yield continuation for a pending delegated child await.
    // This is derived from the validated bytecode lowering shape.
    int delegatedPendingYieldPc = -1;
    bool delegatedChildPending = false;
    bool done = false;
    bool started = false;

    ScriptGenerator() = default;

    explicit ScriptGenerator(const Function* fn) : frame(fn) {}
};

} // namespace arp::scriptvm

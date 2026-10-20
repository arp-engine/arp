#pragma once

// ArpScript: bytecode virtual machine.
//
// Thick-bridge design: every value is a reflection::object, so arithmetic,
// comparison, indexing and method dispatch route through object's built-in
// operators (__add__/__eq__/__lt__/...) and instance/hlist/hdict accessors.
//
// Phase 1: core execution (scalars, control flow, calls, closures, containers).
// Phase 6: host bridge (CallHost), Yield/Suspend.

#include "core/cpu_allocator.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/execution_session.h"
#include "scriptvm/interpreter/frame.h"
#include "scriptvm/interpreter/suspended_frame.h"
#include "scriptvm/ir/typed_ir.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/global_storage.h"
#include "scriptvm/runtime/host.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/value.h"
#include "scriptvm/runtime/vm_error.h"

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace arp::scriptvm {

// Cross-backend direct script calls are resolved by the owning hybrid
// dispatcher. The target is a stable function id when emitted by AOT and a
// source function name when emitted by bytecode; the dispatcher accepts both.
using ScriptCallBridgeFn = Value (*)(
    void*, const core::pmr::String&, const arp::core::pmr::Vector<Value>&, VmError*);

// Yield result: when a script function executes Yield, the VM suspends
// and returns a Suspension to the caller. The caller can resume later.
struct Suspension {
    int frameIndex = -1; // index into the VM's frame stack to resume from
    Value yieldedValue;
};

class Vm {
public:
    Vm();

    // Set the host registry for CallHost opcode support.
    void setHostRegistry(HostRegistry* registry) { hostRegistry_ = registry; }

    // Set the module registry for LoadModule/LoadModuleExport opcode support.
    void setModuleRegistry(ModuleRegistry* registry) { moduleRegistry_ = registry; }

    // Keeps the immutable module program alive for generators created by a
    // dispatcher. Standalone VM callers may leave this unset and retain the
    // existing caller-owned Program lifetime contract.
    void setProgramOwner(std::shared_ptr<const Program> program)
    {
        programOwner_ = std::move(program);
    }

    // Only a validated private snapshot can skip repeated whole-module hashing.
    // Passing any other Program to the public entrypoints retains validation.
    ScriptResult bindProgram(const ValidatedInterpreterProgram& program);

    // Dispatcher-owned VMs pass their shared owner to generators so an old
    // module version remains resumable after a module replacement.
    void setOwner(const std::shared_ptr<Vm>& owner) { owner_ = owner; }

    void setRetentionBudget(std::shared_ptr<ScriptModuleRetentionBudget> budget)
    {
        retentionBudget_ = std::move(budget);
    }

    void setModuleIdentity(core::pmr::String moduleId, std::uint32_t moduleVersion)
    {
        moduleId_ = std::move(moduleId);
        moduleVersion_ = moduleVersion;
    }

    // Bind the opaque artifact token shared with a DirectNative package for
    // the explicitly negotiated reverse generator bridge. Leaving it empty
    // keeps cross-backend adoption fail-closed.
    void setBridgeArtifactIdentity(core::pmr::String artifactIdentity)
    {
        bridgeArtifactIdentity_ = std::move(artifactIdentity);
    }

    void setCallBridge(ScriptCallBridgeFn bridge, void* userData)
    {
        callBridge_ = bridge;
        callBridgeUserData_ = userData;
    }

    // Bind module globals shared with a Hybrid AOT context. The owner must
    // outlive this Vm and keep the vector stable while execution is active.
    void setSharedGlobals(arp::core::pmr::Vector<Value>* globals) { sharedGlobals_ = globals; }

    // Bind a complete immutable Program artifact to its module-owned globals.
    // The shared_ptr keeps the storage alive across adopted foreign frames.
    void setProgramGlobalStorage(
        std::shared_ptr<const Program> program, std::shared_ptr<ScriptModuleGlobalStorage> storage);

    // A negative budget disables the limit. The budget is shared by nested
    // script calls in one top-level invocation.
    void setInstructionBudget(std::int64_t budget) { instructionBudget_ = budget; }

    void setExecutionSession(std::shared_ptr<ScriptExecutionSession> session)
    {
        executionSession_ = std::move(session);
    }

    // A negative limit disables the guard; zero rejects the first frame.
    // Nested calls consume one frame each and report a script diagnostic
    // instead of using native stack depth as the contract.
    void setRecursionLimit(int limit) { recursionLimit_ = limit; }

    void setMaxFrameValues(std::size_t limit) { maxFrameValues_ = limit; }

    void setMaxContainerValues(std::size_t limit) { maxContainerValues_ = limit; }

    void setMaxCallArguments(std::size_t limit) { maxCallArguments_ = limit; }

    void setMaxContinuationFrames(std::size_t limit) { maxContinuationFrames_ = limit; }

    std::size_t maxContinuationFrames() const noexcept { return maxContinuationFrames_; }

    void setMemoryQuota(std::shared_ptr<core::CpuMemoryQuota> quota)
    {
        memoryQuota_ = std::move(quota);
    }

    // Execute a program's entry function and return the single result value.
    // Returns the result on success or fills `err` on failure.
    Value run(const Program& program, VmError* err = nullptr);

    // Execute a specific function by index with the given arguments.
    Value runFunction(const Program& program,
        int funcIndex,
        const arp::core::pmr::Vector<Value>& args,
        VmError* err = nullptr);

    // Execute a specific function by name.
    Value runFunctionByName(const Program& program,
        core::pmr::String name,
        const arp::core::pmr::Vector<Value>& args,
        VmError* err = nullptr);

    // Invoke a script-declared instance method. The receiver is prepended
    // using the same calling convention as the ScriptMethod value produced by
    // LoadField, so retained script objects can be driven across frames.
    Value runMethodByName(const Program& program,
        const Value& receiver,
        core::pmr::String className,
        core::pmr::String methodName,
        const arp::core::pmr::Vector<Value>& args,
        VmError* err = nullptr);

    // Resume the most recent suspended await. The caller must dispatch the
    // owning async runtime before calling this method.
    Value resume(const Program& program, VmError* err = nullptr);

    bool suspended() const noexcept { return suspendedFrame_.function != nullptr; }

    // Snapshot only the currently active logical script frames. Hybrid uses
    // this at a backend bridge to preserve the caller portion of a traceback.
    bool hasActiveFrames() const noexcept { return !frames_.empty(); }

    core::pmr::String logicalTraceback() const;

    // Resume a generator value owned by this VM. Exposed for backend-neutral
    // range iteration when an AOT caller receives an interpreter generator.
    Value resumeGeneratorValue(ScriptGenerator& generator, VmError& err);
    // Resume a yielded generator with one send() value. The value is pushed
    // at the next yield boundary and consumed once by the frame.
    Value resumeGeneratorValueSend(ScriptGenerator& generator, const Value& value, VmError& err);
    // throw() injects a UserRaise at the current yielded boundary and lets
    // the generator's existing script handlers decide whether to catch it.
    Value resumeGeneratorValueThrow(ScriptGenerator& generator, const Value& value, VmError& err);
    // close() permanently exhausts a generator and discards pending await
    // state.
    void closeGenerator(ScriptGenerator& generator) noexcept;

    bool hasActiveGenerator() const noexcept { return activeGenerator_ != nullptr; }

    ScriptResult snapshotGeneratorFrame(const ScriptGenerator& generator,
        const core::pmr::String& moduleId,
        std::uint32_t moduleVersion,
        SuspendedScriptFrame& out) const;
    ScriptResult adoptGeneratorFrame(
        ScriptGenerator& generator, const Program& program, const SuspendedScriptFrame& frame);

    // Materialize the currently pending await continuation without exposing
    // the backend-owned Frame or native program pointer.
    ScriptResult snapshotSuspendedFrame(const core::pmr::String& moduleId,
        std::uint32_t moduleVersion,
        SuspendedScriptFrame& out) const;

    // Adopt a validated await frame produced by another backend. The frame
    // must identify an Await safepoint in this Program; arbitrary native PCs
    // and incomplete mappings are rejected.
    ScriptResult adoptSuspendedFrame(const Program& program, const SuspendedScriptFrame& frame);

    // Drop a continuation only after another backend has adopted its
    // validated canonical state.
    void discardSuspendedFrame();

private:
    bool validateForExecution(const Program& program, VmError& error) const;
    ValidatedInterpreterProgram validatedProgram_;
    // Core executor: returns one Value. Multiple return values are represented
    // as a list Value; `produced` distinguishes void from nil.
    Value executeFrame(
        const Program& program, Frame&& frame, VmError& err, bool* produced = nullptr);
    Value executeFrameRef(const Program& program,
        Frame& frame,
        VmError& err,
        bool* produced = nullptr,
        bool injectError = false,
        ExecutionSlot* slotResult = nullptr);
    enum class ScalarDispatchStatus { Returned, Replay, Failed };
    ScalarDispatchStatus executeScalarDispatch(const Program& program,
        Frame& frame,
        VmError& error,
        Value* result,
        bool* produced,
        ExecutionSlot* slotResult);
    bool executeScalarArgumentLeaf(const Function& callee,
        const ScalarExpressionPlan& leaf,
        Frame& caller,
        std::size_t argumentCount,
        bool hasReceiver,
        std::int64_t& remaining);

    static int resolveScriptMethod(
        const Program& program, const Instruction& instruction, const Value& receiver)
    {
        int index = instruction.a;
        if (!instruction.s.empty()) {
            const auto* actual = checkedValuePayload<arp::reflection::instance>(receiver);
            if (actual != nullptr && !actual->alive())
                return -1;
            const auto* classInfo = actual ? actual->class_() : nullptr;
            if (classInfo != nullptr) {
                const auto methods = program.methodSets.find(classInfo->name());
                if (methods != program.methodSets.end()) {
                    const auto method = methods->second.find(instruction.s);
                    if (method != methods->second.end())
                        index = method->second;
                }
            }
        }
        return index;
    }

    bool admitsScalarArgumentLeaf(const Function& callee,
        int cost,
        std::size_t argumentCount,
        bool hasReceiver,
        std::int64_t remaining) const
    {
        return !(memoryQuota_ != nullptr && (hasReceiver || callee.params != 1))
            && argumentCount + hasReceiver <= maxCallArguments_
            && (remaining < 0 || remaining >= cost)
            && static_cast<std::size_t>(callee.locals) <= maxFrameValues_
            && static_cast<std::size_t>(callee.maxStack) <= maxFrameValues_
            && !(executionSession_ == nullptr && recursionLimit_ >= 0
                && frames_.size() >= static_cast<std::size_t>(recursionLimit_));
    }

    bool executeScalarMethodLoop(const Program& program,
        Frame& frame,
        const ScalarExpressionPlan& plan,
        const ScalarExpressionPlan::Entry& entry,
        std::int64_t& remaining);
    bool executeScalarClosureLoop(const Program& program,
        Frame& frame,
        const ScalarExpressionPlan& plan,
        const ScalarExpressionPlan::Entry& entry,
        std::int64_t& remaining);

    bool admitsScalarCaptureLeaf(const Function& callee,
        const ScalarExpressionPlan::CaptureLeaf& leaf,
        const ScriptClosure& closure,
        std::size_t argumentCount,
        std::int64_t remaining) const
    {
        return memoryQuota_ == nullptr && argumentCount == static_cast<std::size_t>(callee.params)
            && argumentCount <= maxCallArguments_
            && static_cast<std::size_t>(callee.locals) <= maxFrameValues_
            && static_cast<std::size_t>(callee.maxStack) <= maxFrameValues_
            && (remaining < 0 || remaining >= leaf.cost)
            && !(executionSession_ == nullptr && recursionLimit_ >= 0
                && frames_.size() >= static_cast<std::size_t>(recursionLimit_))
            && static_cast<std::size_t>(leaf.capture) < closure.captures.size()
            && closure.captures[static_cast<std::size_t>(leaf.capture)];
    }

    template <class Finish>
    bool evaluateScalarCaptureLeaf(const Function& callee,
        const ScalarExpressionPlan::CaptureLeaf& leaf,
        const ScriptClosure& closure,
        std::size_t argumentCount,
        std::int64_t remaining,
        Finish finish)
    {
        if (argumentCount != callSlots_.size()
            || !admitsScalarCaptureLeaf(callee, leaf, closure, argumentCount, remaining))
            return false;
        ScriptExecutionFrameScope logicalFrame(executionSession_.get());
        if (!logicalFrame.active())
            return false;
        int result;
        if (!leaf.evaluate(callSlots_[static_cast<std::size_t>(leaf.argument)],
                closure.captures[static_cast<std::size_t>(leaf.capture)]->value,
                result))
            return false;
        // The original logical scope includes return publication. A closed
        // caller may instead consume this result in its pure transaction.
        finish(result);
        return true;
    }

    bool executeScalarCaptureLeaf(const Function& callee,
        const ScalarExpressionPlan::CaptureLeaf& leaf,
        const ScriptClosure& closure,
        Frame& caller,
        std::size_t argumentCount,
        std::int64_t& remaining);
    template <bool StaticValidated, bool StackProven, bool CallLoops = false>
    Value executeFrameImpl(const Program& program,
        Frame& frame,
        VmError& err,
        bool* produced,
        bool injectError,
        ExecutionSlot* slotResult);

    // Run a function: build a frame, copy args into locals[0..params-1],
    // set captures, then executeFrame.
    Value runFunctionInternal(const Program& program,
        int funcIndex,
        const ExecutionArgumentsView& args,
        const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
        VmError* err,
        bool* produced = nullptr,
        ExecutionSlot* slotResult = nullptr,
        bool provenStaticCall = false);

    struct SlotCallComplete {};

    // Exception-only physical cleanup; the original logical execution scope
    // remains in the caller and outlives this guard.
    class FunctionFrameLifetime final {
        friend class Vm;

        explicit FunctionFrameLifetime(Vm& vm) noexcept : vm_(vm) {}

        ~FunctionFrameLifetime();
        FunctionFrameLifetime(const FunctionFrameLifetime&) = delete;
        FunctionFrameLifetime& operator=(const FunctionFrameLifetime&) = delete;
        Vm& vm_;
        Frame* frame_ = nullptr;
        bool published_ = false;
    };

    void discardFunctionFrame(FunctionFrameLifetime& activation) noexcept;
    template <bool SlotReturn>
    using InternalReturn = std::conditional_t<SlotReturn, SlotCallComplete, Value>;
    template <bool SlotReturn>
    InternalReturn<SlotReturn> runFunctionInternalBoundary(const Program& program,
        int funcIndex,
        const ExecutionArgumentsView& args,
        const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
        VmError* err,
        bool* produced,
        ExecutionSlot* slotResult,
        bool provenStaticCall);
    void runStaticFunctionSlots(const Program& program,
        int funcIndex,
        const ExecutionArgumentsView& args,
        const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
        VmError* err,
        bool* produced,
        ExecutionSlot* slotResult);
    template <bool ProvenStaticCall, bool SlotReturn = false>
    InternalReturn<SlotReturn> runFunctionInternalImpl(const Program& program,
        int funcIndex,
        const ExecutionArgumentsView& args,
        const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
        VmError* err,
        bool* produced = nullptr,
        ExecutionSlot* slotResult = nullptr);

    core::pmr::Vector<ExecutionSlot> callSlots_{&core::defaultCpuMemoryResource()};
    bool collectSlotArguments(
        Frame& frame, int count, VmError& error, const char* underflowMessage);

    bool createGenerator(const Program& program,
        int funcIndex,
        const arp::core::pmr::Vector<Value>& args,
        const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
        Value& out,
        VmError& err);
    Value resumeGenerator(ScriptGenerator& generator, VmError& err, bool injectError = false);
    // Resolve a pending await through its explicit host injection contract.
    // Returns false when the generator is not suspended at an await boundary;
    // true means the caller must either return the populated error or resume
    // through resumeGenerator() when the callback produced a value.
    bool injectPendingGeneratorAwait(ScriptGenerator& generator,
        ScriptAwaitInjectionKind kind,
        const Value& value,
        VmError& err);

    void buildTraceback(const Program& program, VmError& err) const;

    // Globals storage, lazily sized to program.globalCount.
    arp::core::pmr::Vector<Value> globals_ = arp::core::pmr::Vector<Value>(
        arp::core::pmr::Vector<Value>::allocator_type(&arp::core::defaultCpuMemoryResource()));
    arp::core::pmr::Vector<Value>* sharedGlobals_ = nullptr;

    arp::core::pmr::Vector<Value>& globalStorage(const Program& program);
    // Non-owning call-stack view used only for traceback construction. Frames
    // are owned by runFunctionInternal's local scope or framePool_.
    arp::core::pmr::Vector<Frame*> frames_{&arp::core::defaultCpuMemoryResource()};

    // Frame storage owns each allocated frame for the VM lifetime. The free
    // list only stores pointers, so recursive calls do not move the PMR
    // vectors in a Frame on entry or return.
    arp::core::pmr::Vector<arp::core::UniquePtr<Frame>> frameStorage_{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<Frame*> framePool_{&arp::core::defaultCpuMemoryResource()};

    // Reusable argument buffer for Call/CallValue opcodes.
    // Avoids per-call PMR vector allocation.
    arp::core::pmr::Vector<Value> callArgs_ = arp::core::pmr::Vector<Value>(
        arp::core::pmr::Vector<Value>::allocator_type(&arp::core::defaultCpuMemoryResource()));

    // Host registry (optional, set by caller for CallHost support).
    HostRegistry* hostRegistry_ = nullptr;

    // Module registry (optional, set by caller for LoadModule support).
    ModuleRegistry* moduleRegistry_ = nullptr;
    std::shared_ptr<const Program> programOwner_;

    struct ProgramGlobalBinding {
        std::shared_ptr<const Program> program;
        std::shared_ptr<ScriptModuleGlobalStorage> storage;
    };

    arp::core::pmr::Vector<ProgramGlobalBinding> programGlobalBindings_{
        &arp::core::defaultCpuMemoryResource()};
    // Module initializers establish persistent global object graphs. Keep
    // their execution state separate from value storage so an entry call
    // cannot recreate the module on every gameplay tick.
    arp::core::pmr::Vector<const Program*> initializedPrograms_{
        &arp::core::defaultCpuMemoryResource()};
    std::shared_ptr<ScriptModuleRetentionBudget> retentionBudget_;
    std::weak_ptr<Vm> owner_;
    core::pmr::String moduleId_{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion_ = 0;
    core::pmr::String bridgeArtifactIdentity_{&arp::core::defaultCpuMemoryResource()};
    ScriptCallBridgeFn callBridge_ = nullptr;
    void* callBridgeUserData_ = nullptr;
    std::int64_t instructionBudget_ = -1;
    std::int64_t remainingInstructions_ = -1;
    std::shared_ptr<ScriptExecutionSession> executionSession_;
    int recursionLimit_ = 1024;
    std::size_t maxFrameValues_ = 65536;
    std::size_t maxContainerValues_ = 1u << 20;
    std::size_t maxCallArguments_ = 4096;
    std::size_t maxContinuationFrames_ = 1024;
    std::shared_ptr<core::CpuMemoryQuota> memoryQuota_;

    // A private synchronous call may inherit the exact binding installed by
    // its caller. Public/reentrant entries always install a fresh core scope;
    // changing the quota or its ownership invalidates inheritance.
    class ExecutionQuotaBinding final {
    public:
        explicit ExecutionQuotaBinding(Vm& vm) noexcept
            : vm_(vm), previous_(vm.quotaBinding_), scope_(vm.memoryQuota_)
        {
            vm_.quotaBinding_ = this;
        }

        ~ExecutionQuotaBinding()
        {
            assert(vm_.quotaBinding_ == this);
            vm_.quotaBinding_ = previous_;
        }

        ExecutionQuotaBinding(const ExecutionQuotaBinding&) = delete;
        ExecutionQuotaBinding& operator=(const ExecutionQuotaBinding&) = delete;

        bool matches(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
        {
            return scope_.matchesQuota(quota);
        }

        const IntegerValueCacheBorrow& integerCache() noexcept
        {
            // Bind at the first actual cached-integer ABI edge, after ordinary
            // admission. Reentry gets its own scope and restores this live view.
            if (!integerCache_.valid())
                integerCache_ = IntegerValueCacheBorrow::current();
            return integerCache_;
        }

    private:
        Vm& vm_;
        ExecutionQuotaBinding* previous_;
        core::CpuMemoryQuotaScope scope_;
        IntegerValueCacheBorrow integerCache_;
    };

    ExecutionQuotaBinding* quotaBinding_ = nullptr;

    void ensureGlobals(const Program& program);
    bool ensureProgramInitialized(const Program& program, VmError& error);

    // Scalar helpers for opcode implementations.
    static bool popInt(Frame& f, int& out, VmError& err);
    static bool popInt64(Frame& f, std::int64_t& out, VmError& err);
    static bool popFloat(Frame& f, double& out, VmError& err);
    static bool popBool(Frame& f, bool& out, VmError& err);
    static bool popString(Frame& f, core::pmr::String& out, VmError& err);

    // Pointer helpers: a Value carrying a ScriptPointer.
    static bool popPointer(Frame& f, std::shared_ptr<ScriptCell>& out, VmError& err);
    static bool popScriptPointer(Frame& f, ScriptPointer& out, VmError& err);

    Frame suspendedFrame_;
    Value suspendedAwaitable_;
    bool suspendedAwaiting_ = false;
    const Program* suspendedProgram_ = nullptr;
    // A suspended chain may contain frames from registered foreign modules.
    // Keep the validated program owner for each frame so resume executes the
    // same bytecode artifact that produced the canonical state.
    const Program* suspendedFrameProgram_ = nullptr;
    std::shared_ptr<const Program> suspendedFrameProgramOwner_;
    arp::core::pmr::Vector<const Program*> suspendedCallerPrograms_{
        &arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<std::shared_ptr<const Program>> suspendedCallerProgramOwners_{
        &arp::core::defaultCpuMemoryResource()};
    const Program* activeProgram_ = nullptr;
    std::shared_ptr<const Program> activeProgramOwner_;
    ScriptGenerator* activeGenerator_ = nullptr;
    arp::core::pmr::Vector<Frame> suspendedCallers_{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<std::uint8_t> suspendedCallerPushResults_{
        &arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm

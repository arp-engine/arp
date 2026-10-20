#pragma once

// Backend-neutral suspended state. Backends may only switch at a safepoint
// after this complete state has been materialized; native PCs are not part of
// the contract.

#include "core/cpu_allocator.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/source_pos.h"
#include "scriptvm/runtime/value.h"

#include <cstdint>
#include <memory>

namespace arp::scriptvm {

class DirectNativeContinuationTransferV5;

struct SuspendedScriptCallerFrame {
    // Per-frame module provenance. A continuation may cross a closure
    // boundary, so callers cannot inherit the active frame's module.
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0;
    core::pmr::String artifactIdentity{&core::defaultCpuMemoryResource()};
    ScriptAotModuleHandle aotModule = nullptr;
    std::shared_ptr<void> moduleOwner;
    // Same-process Interpreter program lookup hint. The module identity and
    // artifact identity remain the contract; this pointer is populated only
    // after the receiving dispatcher validates the matching Program.
    std::shared_ptr<const Program> interpreterProgram;
    core::pmr::String functionId{&core::defaultCpuMemoryResource()};
    int bytecodePc = -1;
    SourcePos source;
    core::pmr::Vector<Value> locals{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Value> captures{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<ScriptCell>> captureCells{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Value> operandStack{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<int> handlerStack{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::size_t> handlerStackDepths{&core::defaultCpuMemoryResource()};
};

struct SuspendedScriptFrame {
    // The frame is materialized only at a declared backend-neutral safepoint.
    // bytecodePc is the backend resume PC; suspendedValue is either the value
    // observed at a yield or the awaitable that must be polled before resume.
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0;
    core::pmr::String artifactIdentity{&core::defaultCpuMemoryResource()};
    // Active-frame descriptor and owner are retained when a generator
    // continuation crosses an AOT module boundary. The identity fields remain
    // the serialized contract; this pointer/token pair is only a same-process
    // registry lookup hint and is validated before use.
    ScriptAotModuleHandle aotModule = nullptr;
    std::shared_ptr<void> moduleOwner;
    // Same-process Interpreter program lookup hint; never used without
    // module/artifact identity validation by the receiving dispatcher.
    std::shared_ptr<const Program> interpreterProgram;
    core::pmr::String functionId{&core::defaultCpuMemoryResource()};
    // For a generator whose active frame is a nested ordinary callee, this
    // identifies the generator object that owns the continuation. It is
    // empty for ordinary function continuations and equals functionId for a
    // generator suspended in its own frame.
    core::pmr::String generatorFunctionId{&core::defaultCpuMemoryResource()};
    core::pmr::String safepointId{&core::defaultCpuMemoryResource()};
    int bytecodePc = -1;
    SourcePos source;
    core::pmr::Vector<Value> locals{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Value> captures{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<ScriptCell>> captureCells{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Value> operandStack{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<int> handlerStack{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::size_t> handlerStackDepths{&core::defaultCpuMemoryResource()};
    Value suspendedValue;
    Value resumeValue;
    // A script constructor publishes its preallocated receiver instead of
    // its implementation return. This is part of the canonical active-frame
    // contract because the constructor itself may suspend before returning.
    bool replaceReturnValue = false;
    Value returnValue;
    // Await frames retain an awaitable; an operation-boundary OSR frame has
    // awaiting=false and resumes directly at bytecodePc.
    bool awaiting = false;
    // Root-generator yield-from protocol state. This remains separate from
    // the active frame because a delegated child may own the await.
    int delegatedSourceSlot = -1;
    int delegatedYieldPc = -1;
    bool delegatedActive = false;
    // The parent is parked immediately before RangeNext because its
    // delegated child owns a pending Await. The child object and its
    // backend-owned continuation remain in locals; migration retries the
    // declared RangeNext point instead of replaying any earlier operation.
    bool delegatedChildAwaiting = false;
    // Exact generated RangeNext operation that owns the delegated child.
    int delegatedRangePc = -1;
    // Exact parent Yield continuation for the first value produced after the
    // pending child await is injected.
    int delegatedPendingYieldPc = -1;
    core::pmr::Vector<SuspendedScriptCallerFrame> callers{&core::defaultCpuMemoryResource()};
    // DirectNative adoption is a single-consumer ownership transfer. The
    // opaque ticket is absent from ordinary Interpreter snapshots and keeps a
    // copied native continuation from being resumed by two parents.
    std::shared_ptr<DirectNativeContinuationTransferV5> directNativeTransfer;
};

namespace suspended_frame_detail {

inline bool validHandlerMetadata(const core::pmr::Vector<int>& handlers,
    const core::pmr::Vector<std::size_t>& depths,
    std::size_t operandCount)
{
    if (handlers.size() != depths.size())
        return false;
    for (std::size_t index = 0; index < handlers.size(); ++index) {
        if (handlers[index] < 0 || depths[index] > operandCount)
            return false;
    }
    return true;
}

inline bool validCaptureMetadata(const core::pmr::Vector<Value>& captures,
    const core::pmr::Vector<std::shared_ptr<ScriptCell>>& captureCells)
{
    if (!captureCells.empty() && captureCells.size() != captures.size())
        return false;
    for (const auto& cell : captureCells) {
        if (!cell)
            return false;
    }
    return true;
}

} // namespace suspended_frame_detail

// This is a backend-neutral contract validator. Keeping it inline makes the
// contract available to both the Interpreter object target and the
// DirectNative-only runtime without linking either target to the other.
inline ScriptResult validateSuspendedScriptFrame(const SuspendedScriptFrame& frame)
{
    using namespace suspended_frame_detail;
    if (frame.moduleId.empty() || frame.moduleVersion == 0 || frame.functionId.empty()
        || frame.safepointId.empty() || frame.bytecodePc < 0)
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String("suspended script frame identity is incomplete",
                &core::defaultCpuMemoryResource()));
    if (frame.awaiting && !isScriptAwaitable(frame.suspendedValue))
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String(
                "awaiting suspended frame has no awaitable", &core::defaultCpuMemoryResource()));
    if (!frame.source.valid())
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String("suspended script frame source position is missing",
                &core::defaultCpuMemoryResource()));
    if (!validHandlerMetadata(
            frame.handlerStack, frame.handlerStackDepths, frame.operandStack.size()))
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String(
                "suspended handler metadata is incomplete", &core::defaultCpuMemoryResource()));
    if (!validCaptureMetadata(frame.captures, frame.captureCells))
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String(
                "suspended capture cell metadata is invalid", &core::defaultCpuMemoryResource()));
    if (frame.delegatedActive
        && (frame.delegatedSourceSlot < 0
            || (frame.delegatedChildAwaiting && frame.delegatedRangePc < 0)
            || (!frame.delegatedChildAwaiting && frame.delegatedYieldPc < 0)))
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String("suspended delegated generator metadata is invalid",
                &core::defaultCpuMemoryResource()));
    if (frame.delegatedChildAwaiting
        && (frame.awaiting || !frame.delegatedActive || frame.delegatedSourceSlot < 0
            || frame.delegatedSourceSlot >= static_cast<int>(frame.locals.size())
            || frame.delegatedPendingYieldPc < 0))
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String("suspended delegated child-await metadata is invalid",
                &core::defaultCpuMemoryResource()));
    for (const auto& caller : frame.callers) {
        if (caller.moduleId.empty() || caller.moduleVersion == 0 || caller.functionId.empty()
            || caller.bytecodePc < 0)
            return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
                core::pmr::String(
                    "suspended caller identity is incomplete", &core::defaultCpuMemoryResource()));
        if (!validHandlerMetadata(
                caller.handlerStack, caller.handlerStackDepths, caller.operandStack.size()))
            return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
                core::pmr::String("suspended caller handler metadata is invalid",
                    &core::defaultCpuMemoryResource()));
        if (!validCaptureMetadata(caller.captures, caller.captureCells))
            return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
                core::pmr::String("suspended caller capture metadata is invalid",
                    &core::defaultCpuMemoryResource()));
    }
    return ScriptResult::success();
}

} // namespace arp::scriptvm

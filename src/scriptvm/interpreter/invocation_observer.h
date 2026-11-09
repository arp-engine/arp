#pragma once

#include "core/container/array_view.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/runtime/execution_slot.h"
#include "scriptvm/runtime/vm_error.h"

#include <string_view>

namespace arp::scriptvm {
class Vm;

// An opaque, in-process identity for one opt-in tooling run. Never serialized.
struct ScriptExecutionRun final {};

struct ScriptLinkedProgram {
    std::shared_ptr<const ScriptExecutionRun> run;
    std::shared_ptr<const Program> program;
    std::string_view module;
    bool entry = false;
};

// Borrowed only for this synchronous invocation. Parameters are materialized
// frame slots, so inspection cannot consume a caller's borrowed stack suffix.
struct ScriptInvocation {
    Vm& vm;
    const Program& program;
    const ScriptExecutionRun& run;
    int functionIndex;
    core::ArrayView<ExecutionSlot> parameters;
    core::ArrayView<std::shared_ptr<ScriptCell>> captures;
};

// Mechanism only: content, receiver ownership and binding policy belong to the
// caller. The current audited backend is synchronous interpreter execution;
// generators, yield/await and backend bridges are not implicitly authorized.
class ScriptInvocationObserver {
public:
    virtual ~ScriptInvocationObserver() = default;
    virtual bool linkedProgram(const ScriptLinkedProgram&, VmError&) = 0;
    virtual bool beforeInvoke(const ScriptInvocation&, VmError&) = 0;
};

struct ScriptInvocationAudit {
    std::shared_ptr<ScriptInvocationObserver> observer;
    std::shared_ptr<const ScriptExecutionRun> run;
};
} // namespace arp::scriptvm

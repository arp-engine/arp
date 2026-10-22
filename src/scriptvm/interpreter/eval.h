#pragma once

// ArpScript: public evaluation API.
//
// Phase 1 entry points: run a hand-built Program, or run a specific function
// by index/name with arguments. Source-text compilation (compileSource) is
// added in later phases once the lexer/parser/sema/compiler are in place.

#include "core/memory/cpu_allocator.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/value.h"

namespace arp::scriptvm {

struct EvalResult {
    Value value;
    VmError error;

    bool ok() const
    {
        return error.code == ScriptDiagnosticCode::None
            || error.code == ScriptDiagnosticCode::Suspended;
    }

    bool suspended() const { return error.code == ScriptDiagnosticCode::Suspended; }
};

// Run a program's entry function. A module registry is optional for programs
// that contain LoadModule or module member access instructions.
ARP_API EvalResult runProgram(const Program& program, ModuleRegistry* moduleRegistry = nullptr);

// Run a specific function by index with arguments.
ARP_API EvalResult runFunction(const Program& program,
    int funcIndex,
    const arp::core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry = nullptr);

// Run a specific function by name with arguments.
ARP_API EvalResult runFunctionByName(const Program& program,
    core::pmr::String name,
    const arp::core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry = nullptr);

} // namespace arp::scriptvm

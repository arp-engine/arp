#pragma once

#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/ir/typed_ir.h"

namespace arp::scriptvm {

struct InterpreterBytecodeOptions {
    ScriptArtifactIdentity artifactIdentity;
    core::pmr::String entryFunctionId{&core::defaultCpuMemoryResource()};
};

struct BytecodeLoweringReceipt {
    core::pmr::String typedIrFingerprint{&core::defaultCpuMemoryResource()};
    core::pmr::String bytecodeFingerprint{&core::defaultCpuMemoryResource()};
    std::uint32_t bytecodeVersion = 0u;
};

struct BytecodeLoweringResult {
    Program program;
    BytecodeLoweringReceipt receipt;
    ScriptDiagnostic error;

    bool ok() const { return error.code == ScriptDiagnosticCode::None; }
};

class ARP_API InterpreterBytecodeLowerer final {
public:
    static BytecodeLoweringResult lower(
        const TypedIrModule& module, const InterpreterBytecodeOptions& options);
    // Only this exact arithmetic leaf may be evaluated inside an expression.
    // General static calls retain their own frames, quotas and dispatch.
    static bool isScalarArgumentLeaf(const Function& function);
    static bool isScalarCaptureLeaf(const Function& function);
    static bool isScalarDispatch(const Function& function, const Program* program = nullptr);
    static TypedIrOpcode canonicalOpcode(Opcode opcode);
    static TypedIrScalarControlFlowAnalysis analyzeStack(
        const Function& function, const Program* program = nullptr);
    // One-result return/CFG/dependency proof, including recursive components.
    // Dynamic calls, exception handlers and suspension remain excluded.
    static core::pmr::Vector<TypedIrScalarControlFlowAnalysis> analyzeProgramStacks(
        const Program& program);
};

} // namespace arp::scriptvm

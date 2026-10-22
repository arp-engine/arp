#pragma once

#include "core/memory/cpu_allocator.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/artifact_identity.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>

namespace arp::scriptvm {

// Explicit editor-only replacement boundary. Production AOT never routes
// through this service or the Interpreter.
struct InterpreterHotUpdateIdentity {
    ScriptArtifactIdentity artifact;
    core::pmr::String hostAbiHash{&core::defaultCpuMemoryResource()};

    bool complete() const { return artifact.complete() && !hostAbiHash.empty(); }
};

struct InterpreterHotUpdateRequest {
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0;
    std::shared_ptr<Program> program;
    std::shared_ptr<const Program> programOwner;
    InterpreterHotUpdateIdentity identity;

    std::size_t maxRetainedValues = std::numeric_limits<std::size_t>::max();
    std::int64_t instructionBudget = -1;
    int recursionLimit = -1;
    std::size_t maxFrameValues = 65536u;
    std::size_t maxContainerValues = 1u << 20;
    std::size_t maxCallArguments = 4096u;
    std::size_t maxContinuationFrames = 1024u;
    std::uint64_t maxMemoryBytes = 0u;
};

class InterpreterHotUpdateService final {
public:
    InterpreterHotUpdateService() = default;

    static std::shared_ptr<InterpreterHotUpdateService> create(
        InterpreterHotUpdateRequest request, VmError* error = nullptr);

    bool replace(InterpreterHotUpdateRequest request, VmError* error = nullptr);

    Value invoke(
        const char* functionId, const core::pmr::Vector<Value>& args, VmError* error = nullptr);

private:
    static bool validateRequest(const InterpreterHotUpdateRequest& request,
        const InterpreterHotUpdateRequest* current,
        VmError* error);
    static std::shared_ptr<Vm> buildVm(const InterpreterHotUpdateRequest& request,
        const ValidatedInterpreterProgram& program,
        VmError* error);

    mutable std::mutex mutex_;
    InterpreterHotUpdateRequest current_;
    ValidatedInterpreterProgram currentProgram_;
    std::shared_ptr<Vm> vm_;
};

} // namespace arp::scriptvm

#include "scriptvm/editor/editor_hot_update.h"

namespace arp::scriptvm {

namespace {

void clearError(VmError* error)
{
    if (error == nullptr)
        return;
    error->code = ScriptDiagnosticCode::None;
    error->message.clear();
    error->traceback.clear();
    error->moduleId.clear();
    error->moduleVersion = 0;
    error->functionId.clear();
    error->requestedBackend.clear();
    error->selectedBackend.clear();
    error->policyEpoch = 0;
    error->diagnosticId.clear();
}

void fail(VmError* error, ScriptDiagnosticCode code, const char* message)
{
    if (error == nullptr)
        return;
    clearError(error);
    error->code = code;
    error->message = message;
}

bool sameModule(const InterpreterHotUpdateRequest& request, const ScriptArtifactIdentity& artifact)
{
    return request.moduleId == artifact.moduleId && request.moduleVersion == artifact.moduleVersion;
}

} // namespace

bool InterpreterHotUpdateService::validateRequest(const InterpreterHotUpdateRequest& request,
    const InterpreterHotUpdateRequest* current,
    VmError* error)
{
    clearError(error);
    if (request.program == nullptr || request.programOwner == nullptr) {
        fail(error,
            ScriptDiagnosticCode::AotFingerprintMismatch,
            "editor replacement requires a program and its immutable owner");
        return false;
    }

    const auto& artifact = request.program->artifactIdentity;
    if (!sameModule(request, artifact)) {
        fail(error,
            ScriptDiagnosticCode::HybridModuleVersionMismatch,
            "editor replacement module identity does not match the program");
        return false;
    }
    if (!request.identity.complete() || !request.identity.artifact.matches(artifact)
        || request.programOwner.get() != request.program.get()) {
        fail(error,
            ScriptDiagnosticCode::AotFingerprintMismatch,
            "editor replacement artifact, host, or owner identity is invalid");
        return false;
    }
    if (current != nullptr) {
        if (request.moduleId != current->moduleId
            || request.moduleVersion <= current->moduleVersion) {
            fail(error,
                ScriptDiagnosticCode::HybridModuleVersionMismatch,
                "editor replacement must advance the module version");
            return false;
        }
        if (request.identity.hostAbiHash != current->identity.hostAbiHash) {
            fail(error,
                ScriptDiagnosticCode::AotFingerprintMismatch,
                "editor replacement host ABI identity changed");
            return false;
        }
    }
    return true;
}

std::shared_ptr<Vm> InterpreterHotUpdateService::buildVm(const InterpreterHotUpdateRequest& request,
    const ValidatedInterpreterProgram& program,
    VmError* error)
{
    auto vm = core::makeShared<Vm>();
    const auto binding = vm->bindProgram(program);
    if (!binding.ok()) {
        fail(error, binding.diagnostic().code, binding.diagnostic().message.c_str());
        return nullptr;
    }
    vm->setOwner(vm);
    vm->setModuleIdentity(core::pmr::String(request.moduleId, &core::defaultCpuMemoryResource()),
        request.moduleVersion);
    vm->setRetentionBudget(core::makeShared<ScriptModuleRetentionBudget>(
        ScriptModuleRetentionBudget{request.maxRetainedValues, 0u, false}));
    vm->setInstructionBudget(request.instructionBudget);
    vm->setRecursionLimit(request.recursionLimit);
    vm->setMaxFrameValues(request.maxFrameValues);
    vm->setMaxContainerValues(request.maxContainerValues);
    vm->setMaxCallArguments(request.maxCallArguments);
    vm->setMaxContinuationFrames(request.maxContinuationFrames);
    if (request.maxMemoryBytes != 0u) {
        vm->setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(request.maxMemoryBytes));
    }
    return vm;
}

std::shared_ptr<InterpreterHotUpdateService> InterpreterHotUpdateService::create(
    InterpreterHotUpdateRequest request, VmError* error)
{
    if (!validateRequest(request, nullptr, error))
        return nullptr;
    ValidatedInterpreterProgram program;
    const auto validation = ValidatedInterpreterProgram::create(*request.program, program);
    if (!validation.ok()) {
        fail(error, validation.diagnostic().code, validation.diagnostic().message.c_str());
        return nullptr;
    }
    auto service = core::makeShared<InterpreterHotUpdateService>();
    service->vm_ = buildVm(request, program, error);
    if (service->vm_ == nullptr)
        return nullptr;
    service->currentProgram_ = std::move(program);
    service->current_ = std::move(request);
    clearError(error);
    return service;
}

bool InterpreterHotUpdateService::replace(InterpreterHotUpdateRequest request, VmError* error)
{
    std::lock_guard lock(mutex_);
    // A pending await is owned by the current VM until a canonical snapshot
    // is explicitly adopted by another VM. Replacing the VM here would drop
    // that continuation without a close/cancel contract and could replay or
    // lose host-visible work on the next editor invocation.
    if (vm_ != nullptr && vm_->suspended()) {
        fail(error,
            ScriptDiagnosticCode::HybridPlanInvalid,
            "editor replacement requires the current VM continuation to be adopted or closed");
        return false;
    }
    if (!validateRequest(request, &current_, error))
        return false;
    ValidatedInterpreterProgram program;
    const auto validation = ValidatedInterpreterProgram::create(*request.program, program);
    if (!validation.ok()) {
        fail(error, validation.diagnostic().code, validation.diagnostic().message.c_str());
        return false;
    }
    auto replacement = buildVm(request, program, error);
    if (replacement == nullptr) {
        return false;
    }
    vm_ = std::move(replacement);
    currentProgram_ = std::move(program);
    current_ = std::move(request);
    clearError(error);
    return true;
}

Value InterpreterHotUpdateService::invoke(
    const char* functionId, const core::pmr::Vector<Value>& args, VmError* error)
{
    std::shared_ptr<Vm> vm;
    std::shared_ptr<const Program> program;
    {
        std::lock_guard lock(mutex_);
        vm = vm_;
        program = currentProgram_.owner();
    }
    if (vm == nullptr || program == nullptr || functionId == nullptr) {
        fail(error,
            ScriptDiagnosticCode::AotMissingEntry,
            "editor interpreter function is unavailable");
        return Value::undefined();
    }
    return vm->runFunctionByName(
        *program, core::pmr::String(functionId, &core::defaultCpuMemoryResource()), args, error);
}

} // namespace arp::scriptvm

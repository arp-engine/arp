#include "scriptvm/interpreter/eval.h"

namespace arp::scriptvm {

EvalResult runProgram(const Program& program, ModuleRegistry* moduleRegistry)
{
    EvalResult result;
    Vm vm;
    vm.setModuleRegistry(moduleRegistry);
    result.value = vm.run(program, &result.error);
    return result;
}

EvalResult runFunction(const Program& program,
    int funcIndex,
    const arp::core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry)
{
    EvalResult result;
    Vm vm;
    vm.setModuleRegistry(moduleRegistry);
    result.value = vm.runFunction(program, funcIndex, args, &result.error);
    return result;
}

EvalResult runFunctionByName(const Program& program,
    core::pmr::String name,
    const arp::core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry)
{
    EvalResult result;
    Vm vm;
    vm.setModuleRegistry(moduleRegistry);
    result.value = vm.runFunctionByName(program, std::move(name), args, &result.error);
    return result;
}

namespace {
template <class Invoke>
EvalResult evaluateSnapshot(const ValidatedInterpreterProgram& program,
    ModuleRegistry* moduleRegistry,
    std::shared_ptr<const ScriptInvocationAudit> audit,
    Invoke invoke)
{
    EvalResult result;
    Vm vm;
    vm.setModuleRegistry(moduleRegistry);
    vm.setInvocationAudit(std::move(audit));
    const auto binding = vm.bindProgram(program);
    if (!binding.ok()) {
        result.error.code = binding.diagnostic().code;
        result.error.message = binding.diagnostic().message;
        return result;
    }
    result.value = invoke(vm, program.program(), result.error);
    return result;
}
} // namespace

EvalResult runFunction(const ValidatedInterpreterProgram& program,
    int funcIndex,
    const arp::core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry)
{
    return runFunction(program, funcIndex, args, moduleRegistry, {});
}

EvalResult runFunction(const ValidatedInterpreterProgram& program,
    int funcIndex,
    const core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry,
    std::shared_ptr<const ScriptInvocationAudit> audit)
{
    return evaluateSnapshot(program,
        moduleRegistry,
        std::move(audit),
        [&](Vm& vm, const Program& source, VmError& error) {
            return vm.runFunction(source, funcIndex, args, &error);
        });
}

EvalResult runFunctionByName(const ValidatedInterpreterProgram& program,
    core::pmr::String name,
    const arp::core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry)
{
    return runFunctionByName(program, std::move(name), args, moduleRegistry, {});
}

EvalResult runFunctionByName(const ValidatedInterpreterProgram& program,
    core::pmr::String name,
    const core::pmr::Vector<Value>& args,
    ModuleRegistry* moduleRegistry,
    std::shared_ptr<const ScriptInvocationAudit> audit)
{
    return evaluateSnapshot(program,
        moduleRegistry,
        std::move(audit),
        [&](Vm& vm, const Program& source, VmError& error) {
            return vm.runFunctionByName(source, std::move(name), args, &error);
        });
}

} // namespace arp::scriptvm

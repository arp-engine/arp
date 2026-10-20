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

} // namespace arp::scriptvm

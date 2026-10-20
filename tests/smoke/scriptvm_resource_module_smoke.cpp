#include "core/runtime/async_runtime.h"
#include "reflection/reflection.h"
#include "resource/resource_module.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/module_registry.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cassert>
#include <cstdio>

namespace {

struct SmokeResource {
    int value = 0;
};

} // namespace

int main()
{
    arp::reflection::registration::class_<SmokeResource>("SmokeResource")
        .property("value", &SmokeResource::value);

    arp::core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 4u;
    desc.workerCount = 1u;
    desc.workQueueCapacity = 2u;
    desc.completionBudget = 4u;
    arp::core::runtime::AsyncRuntime runtime(desc);
    arp::resource::ResourceManager manager;
    arp::scriptvm::ModuleRegistry modules;
    arp::scriptvm::ScriptPluginServiceRegistry services;
    if (!services.provide(arp::core::pmr::String(arp::scriptvm::kResourceManagerService,
                              &arp::core::defaultCpuMemoryResource()),
            1u,
            &manager)
        || !services.provide(arp::core::pmr::String(arp::scriptvm::kAsyncRuntimeService,
                                 &arp::core::defaultCpuMemoryResource()),
            1u,
            &runtime)) {
        return 1;
    }
    arp::scriptvm::ScriptPluginRegistrar registrar(
        arp::core::pmr::String("resources", &arp::core::defaultCpuMemoryResource()), &services);
    ArpScriptPluginHostV2 host{
        ARP_SCRIPT_PLUGIN_ABI_VERSION_V2, &registrar, &services, "test", "debug", nullptr};
    if (arp::scriptvm::registerResourceScriptPlugin(&host) != 1)
        return 1;
    auto exports = registrar.build();
    if (!exports || !modules.installPluginModule(registrar.name(), std::move(exports)))
        return 1;

    arp::core::pmr::String source{"import resources\n"
                                  "def test() -> int:\n"
                                  "    var result: any = resources.release(\"missing-resource\")\n"
                                  "    return result.success\n",
        &arp::core::defaultCpuMemoryResource()};
    auto compiled = arp::scriptvm::test::buildInterpreter(std::move(source),
        arp::core::pmr::String("resource-smoke.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("resource-smoke", &arp::core::defaultCpuMemoryResource()),
        &modules);
    assert(compiled.ok());

    arp::scriptvm::Vm vm;
    vm.setModuleRegistry(&modules);
    arp::scriptvm::VmError error;
    auto result = vm.runFunctionByName(compiled.program,
        arp::core::pmr::String("test", &arp::core::defaultCpuMemoryResource()),
        {},
        &error);
    assert(error.code == arp::scriptvm::ScriptDiagnosticCode::None);
    bool success = true;
    assert(arp::scriptvm::tryGetBool(result, success));
    assert(!success);
    std::printf("ScriptVM resource module smoke passed\n");
    return 0;
}

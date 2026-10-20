#include "core/runtime/async_runtime.h"
#include "reflection/reflection.h"
#include "resource/resource_module.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/module_registry.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

namespace {

struct ScriptResourcePayload {
    int value = 0;
};

constexpr const char* kResourceId = "58c2d9f1-0b6f-48dd-9a4a-1277ef4b0d81";

arp::core::pmr::String text(const char* value)
{
    return {value, &arp::core::defaultCpuMemoryResource()};
}

arp::core::pmr::String envelope()
{
    const auto hash = arp::resource::computeResourcePayloadHash(
        text("type: ScriptResourcePayload\nfields:\n  value: 17\n"));
    assert(hash.success);
    arp::core::pmr::String result{&arp::core::defaultCpuMemoryResource()};
    result += "format: ArpRuntimeResource\n";
    result += "version: 1\n";
    result += "resourceId: 58c2d9f1-0b6f-48dd-9a4a-1277ef4b0d81\n";
    result += "resourceType: ScriptResourcePayload\n";
    result += "status: Contract Complete\ncontentHash: ";
    result += std::to_string(hash.hash).c_str();
    result += "\npayload:\n  type: ScriptResourcePayload\n  fields:\n    value: 17\n";
    return result;
}

std::string escapedEnvelope()
{
    const auto value = envelope();
    std::string result;
    result.reserve(value.size() * 2u);
    for (const char character : value) {
        if (character == '\n') {
            result += "\\n";
        } else {
            result += character;
        }
    }
    return result;
}

arp::scriptvm::Program compileScript(const char* source)
{
    auto compiled =
        arp::scriptvm::test::buildInterpreter(source, "resource-module.arp", "resource-module");
    assert(compiled.ok());
    return std::move(compiled.program);
}

void registerPayload()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    arp::reflection::registration::class_<ScriptResourcePayload>("ScriptResourcePayload")
        .construct<>()
        .property("value", &ScriptResourcePayload::value);
}

arp::core::runtime::AsyncRuntimeDesc runtimeDesc()
{
    arp::core::runtime::AsyncRuntimeDesc desc;
    desc.completionQueueCapacity = 8u;
    desc.workerCount = 1u;
    desc.workQueueCapacity = 4u;
    desc.completionBudget = 8u;
    return desc;
}

void installResourcePlugin(arp::scriptvm::ModuleRegistry& modules,
    arp::resource::ResourceManager& manager,
    arp::core::runtime::AsyncRuntime& runtime)
{
    arp::scriptvm::ScriptPluginServiceRegistry services;
    assert(services.provide(arp::core::pmr::String(arp::scriptvm::kResourceManagerService,
                                &arp::core::defaultCpuMemoryResource()),
        1u,
        &manager));
    assert(services.provide(arp::core::pmr::String(arp::scriptvm::kAsyncRuntimeService,
                                &arp::core::defaultCpuMemoryResource()),
        1u,
        &runtime));
    arp::scriptvm::ScriptPluginRegistrar registrar(text("resources"), &services);
    ArpScriptPluginHostV2 host{
        ARP_SCRIPT_PLUGIN_ABI_VERSION_V2,
        &registrar,
        &services,
        "test",
        "debug",
        nullptr,
    };
    assert(arp::scriptvm::registerResourceScriptPlugin(&host) == 1);
    auto exports = registrar.build();
    assert(exports != nullptr);
    assert(modules.installPluginModule(registrar.name(), std::move(exports)));
}

void testSyncLoadAndRelease()
{
    registerPayload();
    arp::core::runtime::AsyncRuntime runtime(runtimeDesc());
    arp::resource::ResourceManager manager;
    arp::scriptvm::ModuleRegistry modules;
    installResourcePlugin(modules, manager, runtime);

    const std::string source = std::string("import resources\n") + "def test() -> int:\n"
        + "    var loaded: any = resources.load(\"" + escapedEnvelope() + "\")\n"
        + "    if loaded.success:\n"
        + "        resources.release(\"58c2d9f1-0b6f-48dd-9a4a-1277ef4b0d81\")\n"
        + "    return resources.state(\"58c2d9f1-0b6f-48dd-9a4a-1277ef4b0d81\")\n";
    auto program = compileScript(source.c_str());

    arp::scriptvm::Vm vm;
    vm.setModuleRegistry(&modules);
    arp::scriptvm::VmError error;
    auto result = vm.runFunctionByName(program, text("test"), {}, &error);
    assert(error.code == arp::scriptvm::ScriptDiagnosticCode::None);
    int state = -1;
    assert(arp::scriptvm::tryGetInt(result, state));
    assert(state == static_cast<int>(arp::resource::ResourceState::Unloaded));
}

void testAsyncAwaitResumeAndFailure()
{
    registerPayload();
    arp::core::runtime::AsyncRuntime runtime(runtimeDesc());
    arp::resource::ResourceManager manager;
    arp::scriptvm::ModuleRegistry modules;
    installResourcePlugin(modules, manager, runtime);

    const auto path = "build/win-vulkan-debug/scriptvm_resource_module_test.arpres";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        assert(output.good());
        const auto content = envelope();
        output << content.c_str();
    }

    auto program = compileScript(
        "import resources\n"
        "def load_resource(path: string) -> any:\n"
        "    return await resources.load_async(path)\n"
        "def test() -> int:\n"
        "    var loaded: any = load_resource(\"build/win-vulkan-debug/scriptvm_resource_module_test.arpres\")\n"
        "    if loaded.success:\n"
        "        return resources.state(\"58c2d9f1-0b6f-48dd-9a4a-1277ef4b0d81\")\n"
        "    return -1\n");

    arp::scriptvm::Vm vm;
    vm.setModuleRegistry(&modules);
    arp::scriptvm::VmError error;
    auto first = vm.runFunctionByName(program, text("test"), {}, &error);
    (void)first;
    assert(error.code == arp::scriptvm::ScriptDiagnosticCode::Suspended);
    assert(vm.suspended());

    for (int i = 0; i < 200 && vm.suspended(); ++i) {
        runtime.dispatch();
        if (vm.suspended()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            arp::scriptvm::VmError resumeError;
            auto pending = vm.resume(program, &resumeError);
            if (resumeError.code == arp::scriptvm::ScriptDiagnosticCode::None) {
                int state = -1;
                assert(arp::scriptvm::tryGetInt(pending, state));
                assert(state == static_cast<int>(arp::resource::ResourceState::Ready));
                break;
            }
            assert(resumeError.code == arp::scriptvm::ScriptDiagnosticCode::Suspended);
        }
    }
    assert(!vm.suspended());
    assert(manager.stateOf(kResourceId) == arp::resource::ResourceState::Ready);
    assert(manager.unload(kResourceId).success);

    auto failedProgram = compileScript(
        "import resources\n"
        "def test() -> int:\n"
        "    var loaded: any = await resources.load_async(\"missing-scriptvm-resource.arpres\")\n"
        "    return loaded.success\n");
    arp::scriptvm::Vm failedVm;
    failedVm.setModuleRegistry(&modules);
    arp::scriptvm::VmError failedError;
    (void)failedVm.runFunctionByName(failedProgram, text("test"), {}, &failedError);
    for (int i = 0; i < 200 && failedVm.suspended(); ++i) {
        runtime.dispatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (failedVm.suspended()) {
            (void)failedVm.resume(failedProgram, &failedError);
        }
    }
    assert(failedError.code == arp::scriptvm::ScriptDiagnosticCode::HostError);
    std::remove(path);
    runtime.shutdown(arp::core::runtime::AsyncRuntimeShutdownPolicy::Drain);
}

} // namespace

int main()
{
    testSyncLoadAndRelease();
    testAsyncAwaitResumeAndFailure();
    std::printf("All ScriptVM resource module tests passed\n");
    return 0;
}

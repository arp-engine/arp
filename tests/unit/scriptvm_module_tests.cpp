// ArpScript Module system unit tests (Phase M1).
//
// Tests ScriptPluginRegistrar, ModuleRegistry, and the VM's LoadModule /
// LoadModuleExport opcodes. These tests build a hand-crafted Program that
// uses the new opcodes and verifies the VM correctly loads plugin modules
// and dispatches exported functions via CallValue.
//
// Test style: self-contained main() + require() helpers, no framework.

#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/plugin/plugin_runtime_scope.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/value.h"

#include <cstdio>
#include <cstdlib>

using namespace arp::scriptvm;

namespace {

int g_failures = 0;

int dummyPluginEntry(ArpScriptPluginHostV2*)
{
    return 1;
}

void require(bool cond, const char* label)
{
    if (!cond) {
        std::printf("FAIL: %s\n", label);
        ++g_failures;
    }
}

void requireInt(const Value& v, int expected, const char* label)
{
    int actual = 0;
    if (!tryGetInt(v, actual)) {
        std::printf("FAIL: %s (not an int)\n", label);
        ++g_failures;
        return;
    }
    if (actual != expected) {
        std::printf("FAIL: %s (got %d, want %d)\n", label, actual, expected);
        ++g_failures;
    }
}

// Build a hand-crafted Program whose main function:
//   1. LoadModule("math")
//   2. LoadModuleExport("double")
//   3. StoreLocal 0  (store the function value)
//   4. LoadInt 21
//   5. LoadLocal 0   (reload the function)
//   6. CallValue 1   (call with 1 arg)
//   7. Return 1
Program buildCallModuleExportProgram()
{
    Program program;
    Function fn("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 2; // slot 0 = imported function, slot 1 = unused
    fn.maxStack = 3;

    SourcePos pos;
    // LoadModule("math")
    fn.code.push_back(Instruction(Opcode::LoadModule,
        0,
        0,
        0,
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()),
        pos));
    // LoadModuleExport("double")
    fn.code.push_back(Instruction(Opcode::LoadModuleExport,
        0,
        0,
        0,
        arp::core::pmr::String("double", &arp::core::defaultCpuMemoryResource()),
        pos));
    // StoreLocal 0
    fn.code.push_back(Instruction(Opcode::StoreLocal, 0, 0, 0, {}, pos));
    // LoadInt 21
    fn.code.push_back(Instruction(Opcode::LoadInt, 0, 0, 21, {}, pos));
    // LoadLocal 0 (the function)
    fn.code.push_back(Instruction(Opcode::LoadLocal, 0, 0, 0, {}, pos));
    // CallValue 1
    fn.code.push_back(Instruction(Opcode::CallValue, 0, 1, 0, {}, pos));
    // Return 1
    fn.code.push_back(Instruction(Opcode::Return, 1, 0, 0, {}, pos));

    program.functions.push_back(std::move(fn));
    program.entry = 0;
    program.funcIndex["main"] = 0;
    return program;
}

// Build a program that calls LoadModule with no registry set.
Program buildNoRegistryProgram()
{
    Program program;
    Function fn("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 1;

    SourcePos pos;
    fn.code.push_back(Instruction(Opcode::LoadModule,
        0,
        0,
        0,
        arp::core::pmr::String("nonexistent", &arp::core::defaultCpuMemoryResource()),
        pos));
    fn.code.push_back(Instruction(Opcode::Return, 1, 0, 0, {}, pos));

    program.functions.push_back(std::move(fn));
    program.entry = 0;
    program.funcIndex["main"] = 0;
    return program;
}

// Build a program that loads a module but exports a nonexistent symbol.
Program buildBadExportProgram()
{
    Program program;
    Function fn("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;

    SourcePos pos;
    fn.code.push_back(Instruction(Opcode::LoadModule,
        0,
        0,
        0,
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()),
        pos));
    fn.code.push_back(Instruction(Opcode::LoadModuleExport,
        0,
        0,
        0,
        arp::core::pmr::String("nonexistent_export", &arp::core::defaultCpuMemoryResource()),
        pos));
    fn.code.push_back(Instruction(Opcode::Return, 1, 0, 0, {}, pos));

    program.functions.push_back(std::move(fn));
    program.entry = 0;
    program.funcIndex["main"] = 0;
    return program;
}

void testPluginRegistrar()
{
    // Build a plugin module "math" with "double" and "abs" functions.
    ScriptPluginRegistrar builder(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()));
    builder.exportFunction(arp::core::pmr::String("double", &arp::core::defaultCpuMemoryResource()),
        [](const arp::core::pmr::Vector<Value>& args) -> Value {
            if (args.empty())
                return makeInt(0);
            int v = 0;
            tryGetInt(args[0], v);
            return makeInt(v * 2);
        });
    builder.exportFunction(arp::core::pmr::String("abs", &arp::core::defaultCpuMemoryResource()),
        [](const arp::core::pmr::Vector<Value>& args) -> Value {
            if (args.empty())
                return makeInt(0);
            int v = 0;
            tryGetInt(args[0], v);
            return makeInt(v < 0 ? -v : v);
        });

    auto exports = builder.build();
    require(exports != nullptr, "module: build exports");
    require(exports->symbols.size() == 4, "module: 2 business exports plus ABI metadata");
    require(exports->symbols.find(
                arp::core::pmr::String("abi_version", &arp::core::defaultCpuMemoryResource()))
            != exports->symbols.end(),
        "module: generated abi_version metadata");
    require(exports->symbols.find(
                arp::core::pmr::String("domain", &arp::core::defaultCpuMemoryResource()))
            != exports->symbols.end(),
        "module: generated domain metadata");

    ModuleRegistry registry;
    registry.installPluginModule(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()), exports);

    // Query exports
    auto names = registry.queryExports(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()));
    require(names.size() == 4, "module: queryExports returns business exports and metadata");

    require(
        registry.hasExport(arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("double", &arp::core::defaultCpuMemoryResource())),
        "module: hasExport double");
    require(
        !registry.hasExport(arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("nonexistent", &arp::core::defaultCpuMemoryResource())),
        "module: !hasExport nonexistent");
}

void testLoadModuleExport()
{
    // Build plugin module
    ScriptPluginRegistrar builder(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()));
    builder.exportFunction(arp::core::pmr::String("double", &arp::core::defaultCpuMemoryResource()),
        [](const arp::core::pmr::Vector<Value>& args) -> Value {
            if (args.empty())
                return makeInt(0);
            int v = 0;
            tryGetInt(args[0], v);
            return makeInt(v * 2);
        });

    ModuleRegistry registry;
    registry.installPluginModule(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()), builder.build());

    auto program = buildCallModuleExportProgram();

    Vm vm;
    vm.setModuleRegistry(&registry);
    VmError err;
    Value result = vm.run(program, &err);
    require(err.code == ScriptDiagnosticCode::None, "module: run ok");
    if (err.code == ScriptDiagnosticCode::None)
        requireInt(result, 42, "module: double(21)=42");
}

void testNoModuleRegistry()
{
    auto program = buildNoRegistryProgram();
    Vm vm;
    // No setModuleRegistry call.
    VmError err;
    Value result = vm.run(program, &err);
    require(err.code == ScriptDiagnosticCode::HostError, "module: no registry -> HostError");
    (void)result;
}

void testNonexistentModule()
{
    ModuleRegistry registry;
    // Register a different module
    ScriptPluginRegistrar builder(
        arp::core::pmr::String("other", &arp::core::defaultCpuMemoryResource()));
    builder.exportValue(
        arp::core::pmr::String("x", &arp::core::defaultCpuMemoryResource()), makeInt(1));
    registry.installPluginModule(
        arp::core::pmr::String("other", &arp::core::defaultCpuMemoryResource()), builder.build());

    auto program = buildNoRegistryProgram(); // loads "nonexistent"
    Vm vm;
    vm.setModuleRegistry(&registry);
    VmError err;
    vm.run(program, &err);
    require(err.code == ScriptDiagnosticCode::HostError, "module: nonexistent -> HostError");
}

void testNonexistentExport()
{
    ScriptPluginRegistrar builder(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()));
    builder.exportFunction(arp::core::pmr::String("double", &arp::core::defaultCpuMemoryResource()),
        [](const arp::core::pmr::Vector<Value>&) -> Value {
            return makeInt(0);
        });

    ModuleRegistry registry;
    registry.installPluginModule(
        arp::core::pmr::String("math", &arp::core::defaultCpuMemoryResource()), builder.build());

    auto program = buildBadExportProgram(); // loads "math", exports "nonexistent_export"
    Vm vm;
    vm.setModuleRegistry(&registry);
    VmError err;
    vm.run(program, &err);
    require(err.code == ScriptDiagnosticCode::HostError, "module: bad export -> HostError");
}

void testExportScalarValue()
{
    // Export a plain int scalar.
    ScriptPluginRegistrar builder(
        arp::core::pmr::String("constants", &arp::core::defaultCpuMemoryResource()));
    builder.exportValue(
        arp::core::pmr::String("the_answer", &arp::core::defaultCpuMemoryResource()), makeInt(42));

    ModuleRegistry registry;
    registry.installPluginModule(
        arp::core::pmr::String("constants", &arp::core::defaultCpuMemoryResource()),
        builder.build());

    // Build program: LoadModule("constants") + LoadModuleExport("the_answer") + Return
    Program program;
    Function fn("main");
    fn.params = 0;
    fn.results = 1;
    fn.locals = 0;
    fn.maxStack = 2;
    SourcePos pos;
    fn.code.push_back(Instruction(Opcode::LoadModule,
        0,
        0,
        0,
        arp::core::pmr::String("constants", &arp::core::defaultCpuMemoryResource()),
        pos));
    fn.code.push_back(Instruction(Opcode::LoadModuleExport,
        0,
        0,
        0,
        arp::core::pmr::String("the_answer", &arp::core::defaultCpuMemoryResource()),
        pos));
    fn.code.push_back(Instruction(Opcode::Return, 1, 0, 0, {}, pos));
    program.functions.push_back(std::move(fn));
    program.entry = 0;
    program.funcIndex["main"] = 0;

    Vm vm;
    vm.setModuleRegistry(&registry);
    VmError err;
    Value result = vm.run(program, &err);
    require(err.code == ScriptDiagnosticCode::None, "module: scalar export run ok");
    if (err.code == ScriptDiagnosticCode::None)
        requireInt(result, 42, "module: the_answer=42");
}

void testModuleNamesAreFlatIdentifiers()
{
    StaticPluginCatalog catalog;
    require(
        !catalog.add({arp::core::pmr::String("arp.scene", &arp::core::defaultCpuMemoryResource()),
            2u,
            1u,
            &dummyPluginEntry}),
        "module: dotted names rejected by static catalog");
    require(!catalog.add(
                {arp::core::pmr::String("missing_version", &arp::core::defaultCpuMemoryResource()),
                    2u,
                    0u,
                    &dummyPluginEntry}),
        "module: zero module version rejected by static catalog");
    require(!catalog.add(
                {arp::core::pmr::String("missing_hash", &arp::core::defaultCpuMemoryResource()),
                    2u,
                    1u,
                    &dummyPluginEntry}),
        "module: missing export contract hash rejected by static catalog");
    require(
        catalog.add({arp::core::pmr::String("arp_scene", &arp::core::defaultCpuMemoryResource()),
            2u,
            1u,
            &dummyPluginEntry,
            arp::core::pmr::String("sha256:arp-scene-v2", &arp::core::defaultCpuMemoryResource())}),
        "module: flat identifier accepted by static catalog");
}

void testRuntimeScopeReloadIsTransactional()
{
    ScriptPluginRuntimeScope scope;
    int oldValue = 1;
    require(scope.services().provide(
                arp::core::pmr::String("old.service", &arp::core::defaultCpuMemoryResource()),
                1u,
                &oldValue),
        "runtime scope: seed old service");
    const auto oldGeneration = scope.generation();

    arp::core::pmr::String diagnostics{&arp::core::defaultCpuMemoryResource()};
    const auto rejected = scope.reload(
        [](ModuleRegistry&,
            ScriptPluginServiceRegistry& services,
            StaticPluginCatalog&,
            std::uint64_t,
            arp::core::pmr::String& error) {
            int candidateValue = 2;
            services.provide(
                arp::core::pmr::String("candidate.service", &arp::core::defaultCpuMemoryResource()),
                1u,
                &candidateValue);
            error = "candidate build failed";
            return false;
        },
        diagnostics);
    require(!rejected, "runtime scope: failed candidate rejected");
    require(scope.generation() == oldGeneration,
        "runtime scope: failed candidate preserves generation");
    require(scope.services().find(
                arp::core::pmr::String("old.service", &arp::core::defaultCpuMemoryResource()))
            == &oldValue,
        "runtime scope: failed candidate preserves old service");
    require(scope.services().find(
                arp::core::pmr::String("candidate.service", &arp::core::defaultCpuMemoryResource()))
            == nullptr,
        "runtime scope: failed candidate discards services");
    require(diagnostics == "candidate build failed",
        "runtime scope: failed candidate reports diagnostics");

    int newValue = 3;
    const auto committed = scope.reload(
        [&newValue](ModuleRegistry& modules,
            ScriptPluginServiceRegistry& services,
            StaticPluginCatalog& catalog,
            std::uint64_t generation,
            arp::core::pmr::String&) {
            if (!services.provide(
                    arp::core::pmr::String("new.service", &arp::core::defaultCpuMemoryResource()),
                    1u,
                    &newValue))
                return false;
            if (!catalog.add({arp::core::pmr::String(
                                  "reload_module", &arp::core::defaultCpuMemoryResource()),
                    2u,
                    7u,
                    &dummyPluginEntry,
                    arp::core::pmr::String(
                        "sha256:reload-module-v2", &arp::core::defaultCpuMemoryResource())}))
                return false;
            return modules.loadModule(arp::core::pmr::String(
                       "reload_module", &arp::core::defaultCpuMemoryResource()))
                != nullptr
                && generation > 0u;
        },
        diagnostics);
    require(committed, "runtime scope: valid candidate committed");
    require(
        scope.generation() == oldGeneration + 1u, "runtime scope: commit increments generation");
    require(scope.services().find(
                arp::core::pmr::String("old.service", &arp::core::defaultCpuMemoryResource()))
            == nullptr,
        "runtime scope: commit invalidates old services");
    require(scope.services().find(
                arp::core::pmr::String("new.service", &arp::core::defaultCpuMemoryResource()))
            == &newValue,
        "runtime scope: commit publishes new services");
    const auto loaded = scope.modules().loadModule(
        arp::core::pmr::String("reload_module", &arp::core::defaultCpuMemoryResource()));
    require(loaded != nullptr, "runtime scope: commit publishes candidate catalog");
    require(loaded != nullptr && loaded->exportContractHash == "sha256:reload-module-v2",
        "runtime scope: static catalog preserves export contract hash");
    require(loaded != nullptr && loaded->moduleVersion == 7u,
        "runtime scope: static catalog preserves module version");
}

void testServiceVersionAndDuplicateExportContracts()
{
    ScriptPluginServiceRegistry services;
    int serviceValue = 17;
    require(services.provide(
                arp::core::pmr::String("arp.test.service", &arp::core::defaultCpuMemoryResource()),
                1u,
                &serviceValue),
        "service registry: initial version accepted");
    require(!services.provide(
                arp::core::pmr::String("arp.test.service", &arp::core::defaultCpuMemoryResource()),
                2u,
                &serviceValue),
        "service registry: version mismatch rejected");
    require(
        services.find(
            arp::core::pmr::String("arp.test.service", &arp::core::defaultCpuMemoryResource()), 1u)
            == &serviceValue,
        "service registry: compatible minimum version resolves");
    require(
        services.find(
            arp::core::pmr::String("arp.test.service", &arp::core::defaultCpuMemoryResource()), 2u)
            == nullptr,
        "service registry: incompatible minimum version does not resolve");

    ScriptPluginRegistrar registrar(
        arp::core::pmr::String("duplicate_test", &arp::core::defaultCpuMemoryResource()));
    require(
        registrar.exportValue(
            arp::core::pmr::String("value", &arp::core::defaultCpuMemoryResource()), makeInt(1)),
        "registrar: first export accepted");
    require(
        !registrar.exportValue(
            arp::core::pmr::String("value", &arp::core::defaultCpuMemoryResource()), makeInt(2)),
        "registrar: duplicate export rejected");
}

} // namespace

int main()
{
    testPluginRegistrar();
    testLoadModuleExport();
    testNoModuleRegistry();
    testNonexistentModule();
    testNonexistentExport();
    testExportScalarValue();
    testModuleNamesAreFlatIdentifiers();
    testRuntimeScopeReloadIsTransactional();
    testServiceVersionAndDuplicateExportContracts();

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll ArpScript module tests passed\n");
    return 0;
}

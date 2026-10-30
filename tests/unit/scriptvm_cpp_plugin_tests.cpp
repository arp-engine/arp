#include "core/math/math.h"
#include "scriptvm/plugin/cpp_plugin_importer.h"
#include "tests/fixtures/cpp_plugin_source_fixture.h"
#if ARP_CPP_PLUGIN_DIFFERENTIAL_TEST
#include "scriptvm/interpreter/eval.h"
#include "tests/scriptvm_interpreter_test_fixture.h"
#endif

#include <cstdio>
#include <filesystem>
#include <string>

#ifndef ARP_TEST_CXX_COMPILER
#define ARP_TEST_CXX_COMPILER ""
#endif

#ifndef ARP_TEST_REFLECTION_CODEGEN
#define ARP_TEST_REFLECTION_CODEGEN ""
#endif

#ifndef ARP_TEST_HOST_LIBRARY
#define ARP_TEST_HOST_LIBRARY ""
#endif

namespace {

#define REQUIRE(condition)                                                                         \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(                                                                          \
                stderr, "Requirement failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__);       \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)

using namespace arp::scriptvm;

unsigned shadowCalls = 0;

int shadowEntry(ArpScriptPluginHostV2*)
{
    ++shadowCalls;
    return 0;
}

} // namespace

int main()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    arp::scriptvm::CppPluginImporterDesc desc;
    arp::tests::bindMathPluginSources(desc, true);
    desc.sourceRoot = root / "tests" / "fixtures" / "scriptvm_cpp_plugin";
    desc.cacheRoot = std::filesystem::temp_directory_path() / "arp_scriptvm_cpp_plugin_tests";
    desc.includeRoot = root / "src";
    desc.hostLibrary = ARP_TEST_HOST_LIBRARY;
    const auto pluginCacheRoot = desc.cacheRoot;
    desc.compilerCommand =
        arp::core::pmr::String(ARP_TEST_CXX_COMPILER, &arp::core::defaultCpuMemoryResource());
    desc.reflectionCodegenCommand = ARP_TEST_REFLECTION_CODEGEN;
    const auto repeatedImporterDesc = desc;
    if (desc.compilerCommand.empty()) {
        std::printf("SKIP: ARP_TEST_CXX_COMPILER is empty\n");
        return 0;
    }

    arp::scriptvm::CppPluginImporter importer(std::move(desc));
    arp::scriptvm::ModuleRegistry registry;
    registry.setCppPluginImporter(&importer);

    const auto exports = registry.loadModule(
        arp::core::pmr::String("cpp_math", &arp::core::defaultCpuMemoryResource()));
    if (exports == nullptr) {
        std::fprintf(stderr, "cpp_math load failed: %s\n", importer.lastDiagnostics().c_str());
        return 1;
    }
    REQUIRE(exports != nullptr);
    REQUIRE(exports->symbols.find(
                arp::core::pmr::String("Point", &arp::core::defaultCpuMemoryResource()))
        != exports->symbols.end());
    REQUIRE(exports->reflectionRegistration != nullptr);
    REQUIRE(exports->reflectionRegistration->types().size() == 1u);
    REQUIRE(exports->reflectionRegistration->types().front().name
        == arp::core::pmr::String("Point", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(exports->reflectionRegistration->functions().size() == 1u);
    REQUIRE(exports->reflectionRegistration->functions().front().name
        == arp::core::pmr::String("add", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(
        exports->symbols.find(arp::core::pmr::String("add", &arp::core::defaultCpuMemoryResource()))
        != exports->symbols.end());
    REQUIRE(exports->exportContractHash
        == arp::core::pmr::String("sha256:cpp-math-v2", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(exports->moduleVersion == 1u);
    {
        StaticPluginCatalog catalog;
        REQUIRE(catalog.add({"cpp_math", 2, 1, &shadowEntry, "shadow/1", {}, {}}));
        ModuleRegistry precedence;
        precedence.setCppPluginImporter(&importer);
        precedence.setStaticPluginCatalog(&catalog);
        REQUIRE(precedence.loadModule("cpp_math") == exports && shadowCalls == 0);
    }
    const auto nativeCycle = [&] {
        ModuleRegistry lifetime;
        lifetime.setCppPluginImporter(&importer);
        return lifetime.loadModule("cpp_math") == exports;
    };
    for (unsigned i = 0; i < 20; ++i)
        REQUIRE(nativeCycle());
    const auto beforeNative = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        REQUIRE(nativeCycle());
    const auto afterNative = arp::core::defaultCpuAllocator().stats();
    REQUIRE(beforeNative.liveBytes == afterNative.liveBytes
        && beforeNative.allocationCount - beforeNative.deallocationCount
            == afterNative.allocationCount - afterNative.deallocationCount);
    std::printf("Native plugin registry 20+20 core bytes %llu -> %llu, live delta 0\n",
        static_cast<unsigned long long>(beforeNative.liveBytes),
        static_cast<unsigned long long>(afterNative.liveBytes));

    // Exercise both descriptor sources even in an AOT-only engine build.
    const auto* pointClass = exports->reflectionRegistration->types().front().classInfo;
    REQUIRE(pointClass != nullptr);
    Value reflectedPoint = pointClass->new_object();
    REQUIRE(!isNil(reflectedPoint));
    reflectedPoint.set("adjusted", makeInt(13));
    int reflectedValue = 0;
    REQUIRE(tryGetInt(reflectedPoint["adjusted"], reflectedValue) && reflectedValue == 13);
    REQUIRE(tryGetInt(reflectedPoint["value"], reflectedValue) && reflectedValue == 13);

#if ARP_CPP_PLUGIN_DIFFERENTIAL_TEST
    const auto built = arp::scriptvm::test::buildInterpreter("from cpp_math import Point\n"
                                                             "def main() -> int:\n"
                                                             "    var point: any = Point()\n"
                                                             "    return point.doubleValue()\n",
        "cpp_plugin_differential.arp",
        "cpp_plugin_differential",
        &registry,
        "arp-fn:cpp_plugin_differential/main");
    if (!built.ok()) {
        std::fprintf(stderr, "cpp_math pipeline build failed: %s\n", built.error.message.c_str());
        return 1;
    }
    const auto result = arp::scriptvm::runProgram(built.program, &registry);
    if (!result.ok()) {
        std::fprintf(stderr, "cpp_math execution failed: %s\n", result.error.message.c_str());
        return 1;
    }
    REQUIRE(result.ok());
    int value = 0;
    REQUIRE(arp::scriptvm::tryGetInt(result.value, value));
    REQUIRE(value == 42);
#endif

    // A service-free plugin is immutable and may be reused by a distinct
    // importer without running generated reflection registration a second
    // time against the process-wide registry.
    arp::scriptvm::CppPluginImporter repeatedImporter(repeatedImporterDesc);
    const auto repeatedExports = repeatedImporter.loadModule(
        arp::core::pmr::String("cpp_math", &arp::core::defaultCpuMemoryResource()));
    if (repeatedExports != exports)
        std::fprintf(stderr,
            "cpp_math repeated load failed: %s\n",
            repeatedImporter.lastDiagnostics().c_str());
    REQUIRE(repeatedExports == exports);

    const auto missing = registry.loadModule(
        arp::core::pmr::String("missing", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(missing == nullptr);
    REQUIRE(!registry.lastDiagnostics().empty());

    const auto v1 = registry.loadModule(
        arp::core::pmr::String("cpp_math_v1", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(v1 == nullptr);
    REQUIRE(
        std::string(importer.lastDiagnostics().c_str()).find("contractVersion 1 is incompatible")
        != std::string::npos);

    const auto dotted = registry.loadModule(
        arp::core::pmr::String("cpp.math", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(dotted == nullptr);
    REQUIRE(std::string(importer.lastDiagnostics().c_str()).find("flat identifiers")
        != std::string::npos);

    // The entry lookup happens after both DLLs are loaded. A rejected entry
    // must release that pair so the failed identity can be removed and
    // regenerated; this guards the importer failure-side RAII contract.
    const auto missingEntry = registry.loadModule(
        arp::core::pmr::String("cpp_math_missing_entry", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(missingEntry == nullptr);
    REQUIRE(std::string(importer.lastDiagnostics().c_str()).find("entry point not found")
        != std::string::npos);
    const auto failedModuleCache = pluginCacheRoot / "cpp_math_missing_entry";
    std::error_code failedCleanupError;
    std::filesystem::remove_all(failedModuleCache, failedCleanupError);
    REQUIRE(!failedCleanupError);

    const auto forgedRegistration = registry.loadModule(
        arp::core::pmr::String("cpp_math_forged", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(forgedRegistration == nullptr);
    REQUIRE(std::string(importer.lastDiagnostics().c_str()).find("no explicit domain exports")
        != std::string::npos);
    const auto forgedModuleCache = pluginCacheRoot / "cpp_math_forged";
    std::error_code forgedCleanupError;
    std::filesystem::remove_all(forgedModuleCache, forgedCleanupError);
    REQUIRE(!forgedCleanupError);

    // Exercise the real domain manifest through the dynamic loader. The
    // manifest filename is descriptive, but its module field remains the
    // flat import identifier used by the VM.
#if ARP_CPP_PLUGIN_DIFFERENTIAL_TEST
    arp::scriptvm::CppPluginImporterDesc domainDesc;
    domainDesc.sourceBudget = {16, 1024u * 1024u, 8u * 1024u * 1024u, 8u * 1024u * 1024u};
    domainDesc.sourceBindings.push_back({"arp_core",
        "arp.core_module.arpplugin",
        {{"src/core/math/math.h",
             arp::scriptvm::CppPluginHeaderRoot::IncludeRoot,
             "core/math/math.h"},
            {"src/scriptvm/bindings/script_types.h",
                arp::scriptvm::CppPluginHeaderRoot::IncludeRoot,
                "scriptvm/bindings/script_types.h"}}});
    domainDesc.sourceRoot = root / "modules";
    domainDesc.cacheRoot =
        std::filesystem::temp_directory_path() / "arp_scriptvm_domain_dynamic_plugin_tests";
    domainDesc.includeRoot = root / "src";
    domainDesc.hostLibrary = ARP_TEST_HOST_LIBRARY;
    domainDesc.compilerCommand =
        arp::core::pmr::String(ARP_TEST_CXX_COMPILER, &arp::core::defaultCpuMemoryResource());
    domainDesc.reflectionCodegenCommand = ARP_TEST_REFLECTION_CODEGEN;
    domainDesc.platform = "windows";
    domainDesc.buildConfiguration = "debug";
    arp::scriptvm::CppPluginImporter domainImporter(std::move(domainDesc));
    arp::scriptvm::ModuleRegistry domainRegistry;
    domainRegistry.setCppPluginImporter(&domainImporter);
    // GeneratedSchemaReuse/1 accepts an identical host declaration. A real
    // incompatible host edit must still reject before any replacement.
    auto* conflictingHost = arp::reflection::class_of<arp::core::Vec2>();
    const auto originalField = conflictingHost->get_field("x");
    REQUIRE(originalField.is_property());
    conflictingHost->def("x", arp::reflection::object(101));
    const auto domainCore = domainRegistry.loadModule(
        arp::core::pmr::String("arp_core", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(domainCore == nullptr);
    const std::string domainDiagnostic(domainImporter.lastDiagnostics().c_str());
    if (domainDiagnostic.find("cannot replace an existing class") == std::string::npos) {
        std::fprintf(stderr, "unexpected arp_core rejection: %s\n", domainDiagnostic.c_str());
        return 1;
    }
    REQUIRE(conflictingHost->get_field("x").cast<int>() == 101);
    conflictingHost->def("x", originalField);
    const auto acceptedCore = domainRegistry.loadModule(
        arp::core::pmr::String("arp_core", &arp::core::defaultCpuMemoryResource()));
    REQUIRE(acceptedCore != nullptr);
    REQUIRE(arp::reflection::class_of<arp::core::Vec2>() == conflictingHost);
    REQUIRE(conflictingHost->get_field("x").is_property());
#endif

    // The same immutable contract can be checked before AOT execution. This
    // path deliberately reads ModuleExports and the host service registry;
    // it does not introduce a service-mediated object call path.
    arp::scriptvm::ScriptPluginServiceRegistry services;
    int asyncService = 1;
    REQUIRE(services.provide(
        arp::core::pmr::String("arp.runtime.async", &arp::core::defaultCpuMemoryResource()),
        1u,
        &asyncService));
    registry.setPluginServices(&services);
    auto resourceExports = arp::core::makeShared<arp::scriptvm::ModuleExports>();
    resourceExports->moduleVersion = 1u;
    resourceExports->exportContractHash =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    resourceExports->reflectionRegistryHash =
        "sha256:fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    // installPluginModule accepts a materialized export table, so keep one
    // synthetic symbol in this contract-only fixture instead of weakening
    // the production invariant that an installed plugin must export data.
    resourceExports->symbols.emplace(
        arp::core::pmr::String("ResourceManager", &arp::core::defaultCpuMemoryResource()),
        arp::scriptvm::makeInt(0));
    resourceExports->requiredServices.push_back(
        {arp::core::pmr::String("arp.runtime.async", &arp::core::defaultCpuMemoryResource()), 1u});
    REQUIRE(registry.installPluginModule(
        arp::core::pmr::String("arp_resources", &arp::core::defaultCpuMemoryResource()),
        resourceExports));
    const char* exportHash =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const char* reflectionHash =
        "sha256:fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    const arp::scriptvm::AotServiceDependencyV5 requiredService{"arp.runtime.async", 1u};
    const arp::scriptvm::AotPluginDependencyV5 resourceDependency{
        "arp_resources", 1u, exportHash, reflectionHash, &requiredService, 1u};
    arp::scriptvm::VmError dependencyError;
    REQUIRE(registry.resolveAotPluginDependency(resourceDependency, &dependencyError));
    const arp::scriptvm::AotPluginDependencyV5 staleDependency{
        "arp_resources", 2u, exportHash, reflectionHash, &requiredService, 1u};
    REQUIRE(!registry.resolveAotPluginDependency(staleDependency, &dependencyError));

    std::printf("C++ plugin importer tests passed\n");
    return 0;
}

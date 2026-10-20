#include "core/process_runner.h"
#include "scriptvm/runtime/cpp_plugin_importer.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#ifndef ARP_TEST_CXX_COMPILER
#define ARP_TEST_CXX_COMPILER ""
#endif

#ifndef ARP_TEST_REFLECTION_CODEGEN
#define ARP_TEST_REFLECTION_CODEGEN ""
#endif

#ifndef ARP_TEST_HOST_LIBRARY
#define ARP_TEST_HOST_LIBRARY ""
#endif

arp::scriptvm::CppPluginImporterDesc pluginDesc(const std::filesystem::path& cacheRoot)
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    arp::scriptvm::CppPluginImporterDesc desc;
    desc.sourceRoot = root / "tests" / "fixtures" / "scriptvm_cpp_plugin";
    desc.cacheRoot = cacheRoot;
    desc.includeRoot = root / "src";
    desc.hostLibrary = ARP_TEST_HOST_LIBRARY;
    desc.compilerCommand =
        arp::core::pmr::String(ARP_TEST_CXX_COMPILER, &arp::core::defaultCpuMemoryResource());
    desc.reflectionCodegenCommand = ARP_TEST_REFLECTION_CODEGEN;
    return desc;
}

bool loadPlugin(const std::filesystem::path& cacheRoot,
    arp::core::UniquePtr<arp::reflection::function>* retainedCallable = nullptr,
    std::size_t* exportCount = nullptr)
{
    arp::scriptvm::CppPluginImporter importer(pluginDesc(cacheRoot));
    const auto moduleName =
        arp::core::pmr::String("cpp_math", &arp::core::defaultCpuMemoryResource());
    std::shared_ptr<arp::scriptvm::ModuleExports> first;
    std::shared_ptr<arp::scriptvm::ModuleExports> second;
    std::thread firstLoader([&] {
        first = importer.loadModule(moduleName);
    });
    std::thread secondLoader([&] {
        second = importer.loadModule(moduleName);
    });
    firstLoader.join();
    secondLoader.join();
    if (!first || !second || first != second) {
        std::printf("SMOKE FAIL: %s\n", importer.lastDiagnostics().c_str());
        return false;
    }
    if (first->reflectionRegistration == nullptr
        || first->reflectionRegistration->functions().empty()) {
        std::printf("SMOKE FAIL: generated callable registration is missing\n");
        return false;
    }
    if (exportCount != nullptr)
        *exportCount = first->symbols.size();
    if (retainedCallable != nullptr) {
        *retainedCallable = arp::core::makeUnique<arp::reflection::function>(
            first->reflectionRegistration->functions().front().function);
    }
    return true;
}

int main(int argc, char** argv)
{
    if (ARP_TEST_CXX_COMPILER[0] == '\0') {
        std::printf("SMOKE SKIP: ARP_TEST_CXX_COMPILER is empty\n");
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--concurrent-child")
        return loadPlugin(argv[2]) ? 0 : 1;
    if (argc != 1)
        return 2;

    const auto processCache =
        std::filesystem::temp_directory_path() / "arp_scriptvm_cpp_plugin_process_concurrent";
    std::error_code error;
    std::filesystem::remove_all(processCache, error);
    std::filesystem::create_directories(processCache, error);
    if (error)
        return 3;
    const auto executable = std::filesystem::absolute(argv[0]);
    const auto runChild = [&](const char* name) {
        arp::core::ProcessRunRequest request;
        request.executable = executable;
        request.outputFile = processCache / (std::string(name) + ".log");
        request.arguments.emplace_back("--concurrent-child");
        request.arguments.emplace_back(processCache.string());
        return arp::core::ProcessRunner().run(request);
    };
    arp::core::ProcessRunResult firstProcess;
    arp::core::ProcessRunResult secondProcess;
    std::thread firstProcessThread([&] {
        firstProcess = runChild("first");
    });
    std::thread secondProcessThread([&] {
        secondProcess = runChild("second");
    });
    firstProcessThread.join();
    secondProcessThread.join();
    std::size_t dllCount = 0u;
    std::size_t markerCount = 0u;
    std::size_t metadataCount = 0u;
    std::size_t generatedSourceCount = 0u;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(processCache, error)) {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension();
        dllCount += extension == ".dll" ? 1u : 0u;
        markerCount += extension == ".complete" ? 1u : 0u;
        metadataCount += extension == ".json" ? 1u : 0u;
        generatedSourceCount += extension == ".cpp" ? 1u : 0u;
    }
    if (!firstProcess.ok() || !secondProcess.ok() || error || dllCount != 1u || markerCount != 1u
        || metadataCount != 1u || generatedSourceCount != 1u) {
        std::printf("SMOKE FAIL: concurrent publication is incomplete\n");
        return 4;
    }
    std::filesystem::remove_all(processCache, error);

    arp::core::UniquePtr<arp::reflection::function> retainedCallable;
    std::size_t exportCount = 0u;
    const auto smokeCache =
        std::filesystem::temp_directory_path() / "arp_scriptvm_cpp_plugin_smoke";
    if (!loadPlugin(smokeCache, &retainedCallable, &exportCount))
        return 5;
    const auto result = retainedCallable->call(20, 22);
    if (!result.is<int>() || result.cast<int>() != 42) {
        std::printf("SMOKE FAIL: callable owner did not survive importer destruction\n");
        return 6;
    }
    std::printf("SMOKE OK: C++ plugin module loaded with %zu export(s)\n", exportCount);
    return 0;
}

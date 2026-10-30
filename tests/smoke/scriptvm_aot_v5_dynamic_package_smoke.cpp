#include "core/system/process_runner.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/plugin/aot_dynamic_package.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifndef ARP_AOT_DYNAMIC_TEST_MANIFEST
#error ARP_AOT_DYNAMIC_TEST_MANIFEST is required
#endif
#ifndef ARP_AOT_DYNAMIC_TEST_CACHE
#error ARP_AOT_DYNAMIC_TEST_CACHE is required
#endif
#ifndef ARP_AOT_DYNAMIC_TEST_AOTC
#error ARP_AOT_DYNAMIC_TEST_AOTC is required
#endif
#ifndef ARP_AOT_DYNAMIC_TEST_CXX
#error ARP_AOT_DYNAMIC_TEST_CXX is required
#endif
#ifndef ARP_AOT_DYNAMIC_TEST_NM
#error ARP_AOT_DYNAMIC_TEST_NM is required
#endif
#ifndef ARP_AOT_DYNAMIC_TEST_OBJDUMP
#error ARP_AOT_DYNAMIC_TEST_OBJDUMP is required
#endif
#ifndef ARP_AOT_DYNAMIC_TEST_INCLUDE
#error ARP_AOT_DYNAMIC_TEST_INCLUDE is required
#endif
#ifndef ARP_AOT_DYNAMIC_TEST_HOST
#error ARP_AOT_DYNAMIC_TEST_HOST is required
#endif

namespace {

arp::scriptvm::WindowsDynamicScriptAotPackageDesc dynamicPackageDesc(
    const std::filesystem::path& cacheRoot)
{
    arp::scriptvm::WindowsDynamicScriptAotPackageDesc desc;
    desc.manifest = ARP_AOT_DYNAMIC_TEST_MANIFEST;
    desc.cacheRoot = cacheRoot;
    desc.aotCompiler = ARP_AOT_DYNAMIC_TEST_AOTC;
    desc.cxxCompiler = ARP_AOT_DYNAMIC_TEST_CXX;
    desc.symbolInspector = ARP_AOT_DYNAMIC_TEST_NM;
    desc.objectInspector = ARP_AOT_DYNAMIC_TEST_OBJDUMP;
    desc.includeRoot = ARP_AOT_DYNAMIC_TEST_INCLUDE;
    desc.hostLibrary = ARP_AOT_DYNAMIC_TEST_HOST;
    return desc;
}

bool loadDynamicPackageForProcess(const arp::scriptvm::WindowsDynamicScriptAotPackageDesc& desc,
    arp::scriptvm::WindowsDynamicScriptAotPackageResult* result = nullptr)
{
    using namespace arp::scriptvm;
    ScriptAotPackageRegistry registry;
    auto loaded = WindowsDynamicScriptAotPackageLoader().load(desc, registry);
    const bool valid = loaded.status.ok() && !loaded.packageIdentity.empty()
        && std::filesystem::is_regular_file(loaded.artifactPath)
        && registry.findOwner(loaded.packageIdentity.c_str()) != nullptr;
    if (!valid) {
        std::cerr << "dynamic package child load failed: code="
                  << static_cast<int>(loaded.status.diagnostic().code)
                  << " message=" << loaded.status.diagnostic().message.c_str()
                  << " identity=" << loaded.packageIdentity.c_str()
                  << " artifact=" << loaded.artifactPath.string() << '\n';
    }
    if (result != nullptr)
        *result = std::move(loaded);
    return valid;
}

std::string readDiagnosticLog(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.good())
        return "<missing>";
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv)
{
    using namespace arp::scriptvm;
    if (argc == 3 && std::string_view(argv[1]) == "--concurrent-child")
        return loadDynamicPackageForProcess(dynamicPackageDesc(argv[2])) ? 0 : 1;
    if (argc == 2 && std::string_view(argv[1]) == "--host-text-smoke") {
        auto desc = dynamicPackageDesc(
            std::filesystem::path(ARP_AOT_DYNAMIC_TEST_CACHE).parent_path() / "aot-host-text");
        auto invalid = desc;
        invalid.hostTextBudget.contractVersion = 2;
        ScriptAotPackageRegistry registry;
        const auto rejected = WindowsDynamicScriptAotPackageLoader().load(invalid, registry);
        if (rejected.status.ok()
            || rejected.status.diagnostic().message != "ARP-PLUGIN-HOST-TEXT-CONFIG")
            return 91;
        WindowsDynamicScriptAotPackageResult first, cached;
        if (!loadDynamicPackageForProcess(desc, &first)
            || !loadDynamicPackageForProcess(desc, &cached) || !cached.cacheHit
            || first.packageIdentity != cached.packageIdentity)
            return 92;
        std::puts("PluginHostText/1 native AOT registration and cache audit PASS");
        return 0;
    }
    if (argc != 1)
        return 90;

    const auto desc = dynamicPackageDesc(ARP_AOT_DYNAMIC_TEST_CACHE);

    const auto loadAndInvoke = [](const WindowsDynamicScriptAotPackageDesc& loadDesc,
                                   WindowsDynamicScriptAotPackageResult& loaded) {
        ScriptAotPackageRegistry registry;
        loaded = WindowsDynamicScriptAotPackageLoader().load(loadDesc, registry);
        if (!loaded.status.ok() || loaded.packageIdentity.empty()
            || !std::filesystem::is_regular_file(loaded.artifactPath)
            || loaded.artifactPath.filename().string().size() > 100u
            || registry.findOwner(loaded.packageIdentity.c_str()) == nullptr) {
            std::cerr << "dynamic package load failed: code="
                      << static_cast<int>(loaded.status.diagnostic().code)
                      << " message=" << loaded.status.diagnostic().message.c_str()
                      << " identity=" << loaded.packageIdentity.c_str()
                      << " artifact=" << loaded.artifactPath.string()
                      << " cacheHit=" << (loaded.cacheHit ? 1 : 0) << '\n';
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        ScriptAotModuleInstanceV5 instance;
        if (!createAotOnlyModuleInstanceV5(
                registry, loaded.packageIdentity.c_str(), "basic", instance)
                .ok()) {
            std::cerr << "dynamic package module instance creation failed\n";
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        DirectNativeDispatcherV5 dispatcher(std::move(instance));
        const Value args[] = {makeInt(20), makeInt(22)};
        VmError error;
        int value = 0;
        const auto result = dispatcher.invoke("arp-fn:basic/add", args, 2u, &error);
        if (error.code != ScriptDiagnosticCode::None || !tryGetInt(result, value) || value != 42) {
            std::cerr << "dynamic package add invoke failed: code=" << static_cast<int>(error.code)
                      << " message=" << error.message.c_str() << '\n';
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }

        bool awaitReady = false;
        std::size_t awaitPolls = 0u;
        ScriptAwaitable awaitable;
        awaitable.poll = [&awaitReady, &awaitPolls](Value& awaited, arp::core::pmr::String&) {
            ++awaitPolls;
            if (!awaitReady)
                return ScriptAwaitStatus::Pending;
            awaited = makeInt(70);
            return ScriptAwaitStatus::Ready;
        };
        awaitable.inject = [](ScriptAwaitInjectionKind kind,
                               const Value& injected,
                               Value& awaited,
                               arp::core::pmr::String&) {
            if (kind != ScriptAwaitInjectionKind::Send)
                return ScriptAwaitInjectionStatus::Rejected;
            int value = 0;
            if (!tryGetInt(injected, value))
                return ScriptAwaitInjectionStatus::Failed;
            awaited = makeInt(value + 1);
            return ScriptAwaitInjectionStatus::Ready;
        };
        const Value awaitArgs[] = {Value{awaitable}};
        const Value generatorValue =
            dispatcher.invoke("arp-fn:basic/await_then_yield", awaitArgs, 1u, &error);
        auto* generator = asDirectNativeGeneratorV5(generatorValue);
        if (error.code != ScriptDiagnosticCode::None || generator == nullptr) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        const Value pending = resumeDirectNativeGeneratorV5(*generator, &error);
        if (!isNil(pending) || error.code != ScriptDiagnosticCode::Suspended || awaitPolls != 1u
            || !generator->state->resume.awaitInjectionSupported) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        int injectedResult = 0;
        const Value injected = sendDirectNativeGeneratorV5(*generator, makeInt(9), &error);
        if (!tryGetInt(injected, injectedResult) || injectedResult != 10
            || error.code != ScriptDiagnosticCode::None || awaitPolls != 1u) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        awaitReady = true;
        const Value completed = resumeDirectNativeGeneratorV5(*generator, &error);
        int completedValue = 0;
        if (!tryGetInt(completed, completedValue) || completedValue != 4
            || error.code != ScriptDiagnosticCode::None) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }

        const Value delegatedArgs[] = {makeInt(10)};
        error = {};
        const Value delegatedValue =
            dispatcher.invoke("arp-fn:basic/delegated_parent", delegatedArgs, 1u, &error);
        auto* delegated = asDirectNativeGeneratorV5(delegatedValue);
        if (error.code != ScriptDiagnosticCode::None || delegated == nullptr) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        const Value delegatedYield = resumeDirectNativeGeneratorV5(*delegated, &error);
        int delegatedYieldValue = 0;
        if (!tryGetInt(delegatedYield, delegatedYieldValue) || delegatedYieldValue != 10
            || error.code != ScriptDiagnosticCode::None) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        const Value delegatedResult = sendDirectNativeGeneratorV5(*delegated, makeInt(42), &error);
        int delegatedReturnValue = 0;
        if (!tryGetInt(delegatedResult, delegatedReturnValue) || delegatedReturnValue != 99
            || error.code != ScriptDiagnosticCode::None || delegated->state == nullptr
            || !delegated->state->done) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }

        // Exercise the explicit Any delegation contract through the runtime
        // compiler, including a DirectNative child identity check.
        const Value dynamicChildValue =
            dispatcher.invoke("arp-fn:basic/delegated_child", delegatedArgs, 1u, &error);
        auto* dynamicChild = asDirectNativeGeneratorV5(dynamicChildValue);
        if (error.code != ScriptDiagnosticCode::None || dynamicChild == nullptr)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value dynamicParentArgs[] = {dynamicChildValue};
        const Value dynamicParentValue = dispatcher.invoke(
            "arp-fn:basic/delegated_dynamic_parent", dynamicParentArgs, 1u, &error);
        auto* dynamicParent = asDirectNativeGeneratorV5(dynamicParentValue);
        if (error.code != ScriptDiagnosticCode::None || dynamicParent == nullptr)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        int dynamicValue = 0;
        const Value dynamicYield = resumeDirectNativeGeneratorV5(*dynamicParent, &error);
        if (!tryGetInt(dynamicYield, dynamicValue) || dynamicValue != 10
            || error.code != ScriptDiagnosticCode::None)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value dynamicResult =
            sendDirectNativeGeneratorV5(*dynamicParent, makeInt(42), &error);
        if (!tryGetInt(dynamicResult, dynamicValue) || dynamicValue != 88
            || error.code != ScriptDiagnosticCode::None || dynamicParent->state == nullptr
            || !dynamicParent->state->done)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        error = {};
        const Value closingValue =
            dispatcher.invoke("arp-fn:basic/delegated_parent", delegatedArgs, 1u, &error);
        auto* closing = asDirectNativeGeneratorV5(closingValue);
        if (error.code != ScriptDiagnosticCode::None || closing == nullptr) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        const Value closingYield = resumeDirectNativeGeneratorV5(*closing, &error);
        int closingYieldValue = 0;
        if (!tryGetInt(closingYield, closingYieldValue) || closingYieldValue != 10
            || error.code != ScriptDiagnosticCode::None) {
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        }
        closeDirectNativeGeneratorV5(*closing);
        if (closing->state == nullptr || !closing->state->done)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);

        error = {};
        const Value collectionValue =
            dispatcher.invoke("arp-fn:basic/delegated_collection_parent", nullptr, 0u, &error);
        auto* collection = asDirectNativeGeneratorV5(collectionValue);
        if (error.code != ScriptDiagnosticCode::None || collection == nullptr)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        int collectionValueInt = 0;
        const Value firstCollection = resumeDirectNativeGeneratorV5(*collection, &error);
        if (!tryGetInt(firstCollection, collectionValueInt) || collectionValueInt != 4
            || error.code != ScriptDiagnosticCode::None)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value secondCollection = sendDirectNativeGeneratorV5(*collection, makeNil(), &error);
        if (!tryGetInt(secondCollection, collectionValueInt) || collectionValueInt != 7
            || error.code != ScriptDiagnosticCode::None)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value rejectedCollection =
            sendDirectNativeGeneratorV5(*collection, makeInt(2), &error);
        if (!isNil(rejectedCollection) || error.code != ScriptDiagnosticCode::AotSwitchUnsupported
            || collection->state == nullptr || collection->state->done)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value collectionHandled = throwDirectNativeGeneratorV5(
            *collection, makeString(std::string_view("collection-error")), &error);
        if (!tryGetInt(collectionHandled, collectionValueInt) || collectionValueInt != 8
            || error.code != ScriptDiagnosticCode::None || collection->state == nullptr
            || !collection->state->done)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);

        error = {};
        const Value recursiveArgs[] = {makeInt(2)};
        const Value recursiveValue =
            dispatcher.invoke("arp-fn:basic/recursive_delegated", recursiveArgs, 1u, &error);
        auto* recursive = asDirectNativeGeneratorV5(recursiveValue);
        if (error.code != ScriptDiagnosticCode::None || recursive == nullptr)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value recursiveYield = resumeDirectNativeGeneratorV5(*recursive, &error);
        int recursiveInt = 0;
        if (!tryGetInt(recursiveYield, recursiveInt) || recursiveInt != 1
            || error.code != ScriptDiagnosticCode::None)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value recursiveReturn = resumeDirectNativeGeneratorV5(*recursive, &error);
        if (!tryGetInt(recursiveReturn, recursiveInt) || recursiveInt != 2
            || error.code != ScriptDiagnosticCode::None || recursive->state == nullptr
            || !recursive->state->done || recursive->state->resume.frame != nullptr)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);

        const Value overflowingRecursiveArgs[] = {makeInt(100)};
        error = {};
        const Value overflowingRecursiveValue = dispatcher.invoke(
            "arp-fn:basic/recursive_delegated", overflowingRecursiveArgs, 1u, &error);
        auto* overflowingRecursive = asDirectNativeGeneratorV5(overflowingRecursiveValue);
        if (error.code != ScriptDiagnosticCode::None || overflowingRecursive == nullptr)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        const Value overflowResult = resumeDirectNativeGeneratorV5(*overflowingRecursive, &error);
        if (!isNil(overflowResult) || error.code != ScriptDiagnosticCode::RecursionLimitExceeded
            || overflowingRecursive->state == nullptr || !overflowingRecursive->state->done
            || overflowingRecursive->state->resume.frame != nullptr)
            return (std::cerr << "dynamic check failed line=" << __LINE__ << "\n", false);
        return true;
    };

    WindowsDynamicScriptAotPackageResult first;
    if (!loadAndInvoke(desc, first))
        return 1;
    WindowsDynamicScriptAotPackageResult cached;
    if (!loadAndInvoke(desc, cached) || !cached.cacheHit
        || cached.artifactPath != first.artifactPath) {
        return 2;
    }
    {
        std::ofstream partial(first.artifactPath, std::ios::binary | std::ios::trunc);
        partial << "partial-dll";
    }
    WindowsDynamicScriptAotPackageResult repaired;
    if (!loadAndInvoke(desc, repaired) || repaired.cacheHit
        || repaired.artifactPath != first.artifactPath) {
        std::cerr << "dynamic package repair failed: code="
                  << static_cast<int>(repaired.status.diagnostic().code)
                  << " message=" << repaired.status.diagnostic().message.c_str()
                  << " cacheHit=" << (repaired.cacheHit ? 1 : 0)
                  << " artifact=" << repaired.artifactPath.string()
                  << " expected-artifact=" << first.artifactPath.string() << '\n';
        return 3;
    }
    std::error_code removeError;
    std::filesystem::remove(first.artifactPath.string() + ".complete", removeError);
    if (removeError)
        return 4;
    WindowsDynamicScriptAotPackageResult recovered;
    if (!loadAndInvoke(desc, recovered) || recovered.cacheHit
        || recovered.artifactPath != first.artifactPath) {
        return 5;
    }
    // Relocation must not change a content identity. Keep the same files but
    // use a distinct cache root and a different lexical spelling for inputs.
    const auto lexicalRelocation = [](const std::filesystem::path& path) {
        return path.parent_path() / "." / path.filename();
    };
    WindowsDynamicScriptAotPackageDesc relocated = desc;
    relocated.manifest = lexicalRelocation(desc.manifest);
    relocated.aotCompiler = lexicalRelocation(desc.aotCompiler);
    relocated.cxxCompiler = lexicalRelocation(desc.cxxCompiler);
    relocated.symbolInspector = lexicalRelocation(desc.symbolInspector);
    relocated.objectInspector = lexicalRelocation(desc.objectInspector);
    relocated.includeRoot = desc.includeRoot / ".";
    relocated.cacheRoot =
        desc.cacheRoot.parent_path() / (desc.cacheRoot.filename().string() + "-relocated");
    std::error_code cleanupError;
    std::filesystem::remove_all(relocated.cacheRoot, cleanupError);
    WindowsDynamicScriptAotPackageResult relocatedResult;
    if (!loadAndInvoke(relocated, relocatedResult) || relocatedResult.cacheHit
        || relocatedResult.packageIdentity != first.packageIdentity
        || relocatedResult.artifactPath.filename() != first.artifactPath.filename()) {
        std::filesystem::remove_all(relocated.cacheRoot, cleanupError);
        return 6;
    }

    // The semantic package identity intentionally excludes execution budgets,
    // so the dynamic cache identity must commit the manifest bytes itself.
    // Otherwise a budget-only edit would incorrectly reuse the old DLL.
    const auto budgetVariant = desc.manifest.parent_path() / "package-budget-variant.aot.yaml";
    std::ifstream manifestInput(desc.manifest, std::ios::binary);
    std::string manifestText(std::istreambuf_iterator<char>(manifestInput), {});
    const std::string budgetMarker = "memory-bytes: 1048576";
    const auto budgetPosition = manifestText.find(budgetMarker);
    bool variantReady = budgetPosition != std::string::npos;
    if (variantReady) {
        manifestText.replace(budgetPosition, budgetMarker.size(), "memory-bytes: 1048577");
        manifestText += "\n# cache-identity-variant-";
        manifestText += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        manifestText += "\n";
        std::ofstream manifestOutput(budgetVariant, std::ios::binary | std::ios::trunc);
        manifestOutput.write(
            manifestText.data(), static_cast<std::streamsize>(manifestText.size()));
        variantReady = manifestOutput.good();
    }
    WindowsDynamicScriptAotPackageResult budgetResult;
    if (variantReady) {
        auto budgetDesc = desc;
        budgetDesc.manifest = budgetVariant;
        variantReady = loadAndInvoke(budgetDesc, budgetResult) && !budgetResult.cacheHit
            && budgetResult.artifactPath != first.artifactPath;
    }
    std::filesystem::remove(budgetVariant, cleanupError);
    if (!variantReady) {
        std::filesystem::remove_all(relocated.cacheRoot, cleanupError);
        return 7;
    }
    std::filesystem::remove_all(relocated.cacheRoot, cleanupError);

    WindowsDynamicScriptAotPackageDesc concurrent = desc;
    concurrent.cacheRoot =
        desc.cacheRoot.parent_path() / (desc.cacheRoot.filename().string() + "-threads");
    std::filesystem::remove_all(concurrent.cacheRoot, cleanupError);
    WindowsDynamicScriptAotPackageResult concurrentFirst;
    WindowsDynamicScriptAotPackageResult concurrentSecond;
    bool concurrentFirstOk = false;
    bool concurrentSecondOk = false;
    std::thread firstLoader([&]() {
        concurrentFirstOk = loadAndInvoke(concurrent, concurrentFirst);
    });
    std::thread secondLoader([&]() {
        concurrentSecondOk = loadAndInvoke(concurrent, concurrentSecond);
    });
    firstLoader.join();
    secondLoader.join();
    const bool concurrentOk = concurrentFirstOk && concurrentSecondOk
        && concurrentFirst.packageIdentity == concurrentSecond.packageIdentity
        && std::filesystem::is_regular_file(concurrentFirst.artifactPath)
        && concurrentFirst.artifactPath == concurrentSecond.artifactPath;
    std::filesystem::remove_all(concurrent.cacheRoot, cleanupError);
    if (!concurrentOk)
        return 8;

    // Thread-only contention is insufficient evidence. Exercise two separate
    // process address spaces against the same file-backed identity lock
    // against one identity and require the published artifact to be reusable.
    WindowsDynamicScriptAotPackageDesc processConcurrent = desc;
    processConcurrent.cacheRoot =
        desc.cacheRoot.parent_path() / (desc.cacheRoot.filename().string() + "-processes");
    std::filesystem::remove_all(processConcurrent.cacheRoot, cleanupError);
    std::filesystem::create_directories(processConcurrent.cacheRoot, cleanupError);
    const auto executable = std::filesystem::absolute(argv[0]);
    if (cleanupError || !std::filesystem::is_regular_file(executable))
        return 9;
    const auto runChild = [&](const char* name) {
        arp::core::ProcessRunRequest request;
        request.executable = executable;
        request.workingDirectory = std::filesystem::current_path();
        request.outputFile = processConcurrent.cacheRoot / (std::string(name) + ".log");
        request.arguments.emplace_back("--concurrent-child");
        request.arguments.emplace_back(processConcurrent.cacheRoot.string());
        return arp::core::ProcessRunner().run(request);
    };
    arp::core::ProcessRunResult firstProcess;
    arp::core::ProcessRunResult secondProcess;
    std::thread firstProcessThread([&]() {
        firstProcess = runChild("first");
    });
    std::thread secondProcessThread([&]() {
        secondProcess = runChild("second");
    });
    firstProcessThread.join();
    secondProcessThread.join();
    WindowsDynamicScriptAotPackageResult processCached;
    const bool processConcurrentOk = firstProcess.ok() && secondProcess.ok()
        && loadDynamicPackageForProcess(processConcurrent, &processCached)
        && processCached.cacheHit;
    std::size_t publishedArtifacts = 0u;
    std::size_t completionMarkers = 0u;
    const auto publishedDirectory = processConcurrent.cacheRoot / "packages";
    for (const auto& entry :
        std::filesystem::directory_iterator(publishedDirectory, cleanupError)) {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() == ".dll")
            ++publishedArtifacts;
        if (entry.path().extension() == ".complete")
            ++completionMarkers;
    }
    const bool processPublicationComplete = processConcurrentOk && cleanupError == std::error_code{}
        && publishedArtifacts == 1u && completionMarkers == 1u;
    if (!processPublicationComplete) {
        std::cerr << "dynamic package process publication failed: first="
                  << (firstProcess.ok() ? 1 : 0) << " second=" << (secondProcess.ok() ? 1 : 0)
                  << " cached=" << (processCached.cacheHit ? 1 : 0)
                  << " artifacts=" << publishedArtifacts << " markers=" << completionMarkers
                  << " iterator-error=" << (cleanupError ? cleanupError.message() : "none")
                  << "\nfirst-log:\n"
                  << readDiagnosticLog(processConcurrent.cacheRoot / "first.log")
                  << "\nsecond-log:\n"
                  << readDiagnosticLog(processConcurrent.cacheRoot / "second.log") << '\n';
    }
    std::filesystem::remove_all(processConcurrent.cacheRoot, cleanupError);
    if (!processPublicationComplete)
        return 10;

    return 0;
}

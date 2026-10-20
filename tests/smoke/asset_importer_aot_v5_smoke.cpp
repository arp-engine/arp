#include "core/cpu_allocator.h"
#include "scriptvm/runtime/value.h"
#include "tools/asset_importer/script_runner.h"

#include <cstdio>
#include <filesystem>

#ifndef ARP_SOURCE_DIR
#define ARP_SOURCE_DIR "."
#endif

int main(int argc, char** argv)
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto executable = argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path();
    auto hostLibrary = executable.empty()
        ? root / "build" / "win-vulkan-release" / "arp_script_aot_tool_host.dll"
        : executable.parent_path() / "arp_script_aot_tool_host.dll";
    if (!std::filesystem::exists(hostLibrary)) {
        const auto debugHostLibrary = hostLibrary.parent_path() / "arp_script_aot_tool_hostd.dll";
        if (std::filesystem::exists(debugHostLibrary))
            hostLibrary = debugHostLibrary;
    }
    arp::tools::asset_importer::ScriptToolRunner runner(root,
        root / "modules",
        std::filesystem::temp_directory_path() / "arp_asset_importer_aot_v5_smoke",
        hostLibrary);

    const auto undeclared =
        runner.runFile(root / "tests" / "fixtures" / "asset_importer" / "script_smoke.arp");
    if (undeclared.success
        || undeclared.diagnostics.find("not declared") == arp::core::pmr::String::npos) {
        std::printf(
            "FAIL: undeclared script was not rejected: %s\n", undeclared.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::scriptvm::Value> arguments{&arp::core::defaultCpuMemoryResource()};
    arguments.push_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_scene.yaml").generic_string()));
    arguments.push_back(arp::scriptvm::makeString(root.generic_string()));
    const auto imported = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", arguments);
    int status = -1;
    if (!imported.success || !arp::scriptvm::tryGetInt(imported.value, status) || status != 0) {
        std::printf("FAIL: AOT unity import failed: %s (status=%d)\n",
            imported.diagnostics.c_str(),
            status);
        return 2;
    }

    arp::core::pmr::Vector<arp::scriptvm::Value> addArguments{
        &arp::core::defaultCpuMemoryResource()};
    addArguments.push_back(arp::scriptvm::makeInt(20));
    addArguments.push_back(arp::scriptvm::makeInt(22));
    const auto dynamic = runner.runPackageFunction(
        root / "tests" / "fixtures" / "aot" / "basic.package.yaml", "basic", "add", addArguments);
    int sum = 0;
    if (!dynamic.success || !arp::scriptvm::tryGetInt(dynamic.value, sum) || sum != 42) {
        std::printf("FAIL: dynamic AOT package import failed: %s (sum=%d)\n",
            dynamic.diagnostics.c_str(),
            sum);
        return 3;
    }
    return 0;
}

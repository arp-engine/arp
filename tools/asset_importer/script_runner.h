#pragma once

#include "core/memory/cpu_allocator.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/plugin/cpp_plugin_importer.h"

#include <filesystem>

namespace arp::tools::asset_importer {

struct ScriptRunResult {
    bool success = false;
    scriptvm::Value value;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct ScriptAotToolchain {
    std::filesystem::path aotCompiler;
    std::filesystem::path cxxCompiler;
    std::filesystem::path symbolInspector;
    std::filesystem::path objectInspector;
};

class ScriptToolRunner final {
public:
    ScriptToolRunner(std::filesystem::path projectRoot,
        std::filesystem::path pluginRoot,
        std::filesystem::path cacheRoot,
        std::filesystem::path hostLibrary,
        ScriptAotToolchain aotToolchain = {},
        std::filesystem::path sourceModuleRoot = {});

    ScriptRunResult runFile(const std::filesystem::path& scriptPath,
        const core::pmr::Vector<scriptvm::Value>& args = {});

    ScriptRunResult runFileFunction(const std::filesystem::path& scriptPath,
        const char* functionName,
        const core::pmr::Vector<scriptvm::Value>& args = {});

    ScriptRunResult runPackageFunction(const std::filesystem::path& packageManifest,
        const char* moduleId,
        const char* functionName,
        const core::pmr::Vector<scriptvm::Value>& args = {});

private:
    std::filesystem::path projectRoot_;
    std::filesystem::path pluginRoot_;
    std::filesystem::path cacheRoot_;
    std::filesystem::path hostLibrary_;
    std::filesystem::path sourceModuleRoot_;
    ScriptAotToolchain aotToolchain_;
#if !defined(ARP_SCRIPT_AOT_ONLY_TOOL)
    core::UniquePtr<scriptvm::CppPluginImporter> pluginImporter_;
#endif
};

} // namespace arp::tools::asset_importer

#pragma once

#include "core/memory/cpu_allocator.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/plugin/plugin_host_text_reader.h"

#include <filesystem>

namespace arp::scriptvm {

struct ARP_API WindowsDynamicScriptAotPackageDesc {
    std::filesystem::path manifest;
    std::filesystem::path cacheRoot;
    std::filesystem::path aotCompiler;
    std::filesystem::path cxxCompiler;
    std::filesystem::path symbolInspector;
    std::filesystem::path objectInspector;
    std::filesystem::path includeRoot;
    std::filesystem::path hostLibrary;
    PluginHostTextBudget hostTextBudget;
};

struct ARP_API WindowsDynamicScriptAotPackageResult {
    ScriptResult status;
    core::pmr::String packageIdentity{&core::defaultCpuMemoryResource()};
    std::filesystem::path artifactPath;
    bool cacheHit = false;
};

class ARP_API WindowsDynamicScriptAotPackageLoader final {
public:
    WindowsDynamicScriptAotPackageResult load(
        const WindowsDynamicScriptAotPackageDesc& desc, ScriptAotPackageRegistry& registry) const;
};

} // namespace arp::scriptvm

#pragma once

#include "tools/asset_importer/tool_script_sources.h"

#include <filesystem>

namespace arp::tools::asset_importer {
struct ToolScriptSourcePreparation {
    core::SharedPtr<const ToolScriptSources> sources;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

class ToolScriptSourceHost final {
public:
    // Cold native command-line boundary only. Browser hosts inject FileService
    // into the original asynchronous task and yield between advance/dispatch.
    static ToolScriptSourcePreparation prepareNative(const std::filesystem::path& entry,
        const std::filesystem::path& moduleRoot,
        scriptvm::ScriptSourceBudget = ToolScriptSourceLimits::budget());
};
} // namespace arp::tools::asset_importer

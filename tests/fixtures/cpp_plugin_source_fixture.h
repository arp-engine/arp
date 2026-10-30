#pragma once

#include "scriptvm/plugin/cpp_plugin_importer.h"

namespace arp::tests {

inline void bindMathPluginSources(scriptvm::CppPluginImporterDesc& desc, bool negatives = false)
{
    desc.sourceBudget = {16, 1024u * 1024u, 8u * 1024u * 1024u, 8u * 1024u * 1024u};
    for (const char* name :
        {"cpp_math", "cpp_math_v1", "cpp_math_forged", "cpp_math_missing_entry"}) {
        if (!negatives && core::StringView(name) != "cpp_math")
            continue;
        scriptvm::CppPluginSourceBinding binding;
        binding.module = name;
        binding.manifest = std::filesystem::u8path(core::pmr::String(name) + ".arpplugin");
        binding.headers.push_back(
            {"cpp_math.h", scriptvm::CppPluginHeaderRoot::ManifestDirectory, "cpp_math.h"});
        desc.sourceBindings.push_back(std::move(binding));
    }
}

} // namespace arp::tests

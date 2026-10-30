#pragma once

#include "scriptvm/plugin/cpp_plugin_importer.h"

namespace arp::tools::asset_importer {

// ScriptToolPluginSources/1 names the four tool-owned manifests and headers.
// Paths are metadata bindings, not alternatives selected by filesystem probes.
inline void configureScriptToolPluginSources(scriptvm::CppPluginImporterDesc& desc)
{
    desc.sourceProfile = scriptvm::CppPluginSourceProfile::ExplicitBindingsV1;
    desc.sourceBudget = {64, 16u * 1024u * 1024u, 64u * 1024u * 1024u, 64u * 1024u * 1024u};
    desc.sourceBindings.clear();
    for (const char* name : {"io", "yaml", "asset", "arp_import_host"}) {
        scriptvm::CppPluginSourceBinding binding;
        binding.module = name;
        const core::pmr::String stem(name);
        binding.manifest = std::filesystem::u8path(stem + "/" + stem + ".arpplugin");
        const auto header = stem + "_module.h";
        binding.headers.push_back({header,
            scriptvm::CppPluginHeaderRoot::ManifestDirectory,
            std::filesystem::u8path(header)});
        desc.sourceBindings.push_back(std::move(binding));
    }
}

} // namespace arp::tools::asset_importer

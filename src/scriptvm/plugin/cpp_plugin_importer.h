#pragma once

#include "core/memory/cpu_allocator.h"
#include "scriptvm/plugin/cpp_plugin_api.h"
#include "scriptvm/plugin/plugin_host_text_reader.h"
#include "scriptvm/plugin/script_source_snapshot.h"
#include "scriptvm/runtime/module_registry.h"

#include <filesystem>

namespace arp::scriptvm {

enum class CppPluginSourceProfile { ExplicitBindingsV1, LegacyDiscoveryV1 };
enum class CppPluginHeaderRoot { ManifestDirectory, IncludeRoot };

struct CppPluginHeaderBinding {
    core::pmr::String authoredPath;
    CppPluginHeaderRoot root = CppPluginHeaderRoot::ManifestDirectory;
    std::filesystem::path relativePath;
};

struct CppPluginSourceBinding {
    core::pmr::String module;
    std::filesystem::path manifest;
    core::pmr::Vector<CppPluginHeaderBinding> headers;
};

struct ARP_API CppPluginImporterDesc {
    CppPluginSourceProfile sourceProfile = CppPluginSourceProfile::ExplicitBindingsV1;
    core::pmr::Vector<CppPluginSourceBinding> sourceBindings;
    // Counts preliminary and staged manifest/source/header reads per bounded
    // consistency attempt. Native toolchain capture has separate ownership.
    ScriptSourceBudget sourceBudget;
    PluginHostTextBudget hostTextBudget;
    std::filesystem::path sourceRoot;
    std::filesystem::path cacheRoot;
    std::filesystem::path includeRoot;
    // Explicit project-owned include roots needed by thin plugins. Manifest
    // includeDirs remain sandboxed to the module directory; these roots are
    // supplied by the host contract for shared third-party headers.
    core::pmr::Vector<std::filesystem::path> additionalIncludeRoots{
        &core::defaultCpuMemoryResource()};
    // Runtime host library and its import library directory. The plugin is
    // compiled against this library so reflection and module state stay in
    // the engine ABI rather than being duplicated in the plugin.
    std::filesystem::path hostLibrary;
    // Optional existing reflection code generator. When empty, the importer
    // probes beside hostLibrary for arp_reflect_codegen.
    std::filesystem::path reflectionCodegenCommand;
    core::pmr::String compilerCommand{&core::defaultCpuMemoryResource()};
    // LegacyDiscoveryV1 only. When enabled, each module is stored at
    // <sourceRoot>/<module>/<module>_module.arpplugin. This is explicit so the
    // loader never guesses between flat and module-directory layouts.
    bool moduleDirectoryLayout = false;
    // Runtime-provided services are passed to the v2 plugin registrar.
    ScriptPluginServiceRegistry* services = nullptr;
    core::pmr::String platform{&core::defaultCpuMemoryResource()};
    core::pmr::String buildConfiguration{&core::defaultCpuMemoryResource()};

    CppPluginImporterDesc();
};

// Resolves the module-directory manifest, builds its declared C++ source
// into a platform dynamic library, and loads the declared C ABI entry point.
// This is an explicit Prototype Complete feature: the plugin API intentionally
// exposes only primitive values and callbacks, not arbitrary engine ABI types.
class ARP_API CppPluginImporter final {
public:
    explicit CppPluginImporter(CppPluginImporterDesc desc);
    ~CppPluginImporter();

    CppPluginImporter(const CppPluginImporter&) = delete;
    CppPluginImporter& operator=(const CppPluginImporter&) = delete;
    CppPluginImporter(CppPluginImporter&&) noexcept;
    CppPluginImporter& operator=(CppPluginImporter&&) noexcept;

    std::shared_ptr<ModuleExports> loadModule(const core::pmr::String& moduleName);
    ModuleResolution resolveModule(const core::pmr::String& moduleName);
    CppPluginSourceProfile sourceProfile() const noexcept;
    const core::pmr::String& lastDiagnostics() const;

private:
    struct Impl;
    core::UniquePtr<Impl> impl_;
};

} // namespace arp::scriptvm

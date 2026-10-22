#pragma once

#include "core/memory/cpu_allocator.h"
#include "scriptvm/plugin/cpp_plugin_api.h"
#include "scriptvm/runtime/module_registry.h"

#include <filesystem>

namespace arp::scriptvm {

struct ARP_API CppPluginImporterDesc {
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
    // When enabled, each module is stored at
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
    const core::pmr::String& lastDiagnostics() const;

private:
    struct Impl;
    core::UniquePtr<Impl> impl_;
};

} // namespace arp::scriptvm

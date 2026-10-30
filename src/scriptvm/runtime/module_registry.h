#pragma once

// ArpScript: Module system (import mechanism).
//
// Replaces the flat HostRegistry with a named-module approach. Two module
// sources are supported:
//
// 1. PluginModule: C++ registered through PluginRegistrar. Exports are a
//    name -> Value map (functions, objects, scalars).
// 2. SourceModule: ArpScript .arp files loaded on demand by a resolver
//    callback set on the ModuleRegistry.
//
// The VM's LoadModule opcode queries ModuleRegistry::loadModule(name) and
// pushes a ModuleObject (carrying SharedPtr<ModuleExports>) onto the stack.
// LoadModuleExport pops the ModuleObject, looks up the export name, and
// pushes the exported Value.
//
// Sema can optionally query ModuleRegistry at compile time to verify that
// `from X import Y` names an export that actually exists.

#include "core/concurrency/callback.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/plugin/static_plugin_catalog.h"
#include "scriptvm/runtime/module_exports.h"
#include "scriptvm/runtime/module_resolution.h"

namespace arp::scriptvm {

class CppPluginImporter;

// Registry for plugin modules and source modules.
class ARP_API ModuleRegistry {
public:
    ModuleRegistry() = default;

    // Set the resolver callback for source (.arp) modules.
    // The resolver receives a module name and returns the module's exports.
    // If the module cannot be found, the resolver should return nullptr.
    using SourceResolver = std::function<std::shared_ptr<ModuleExports>(const core::pmr::String&)>;

    void setSourceResolver(SourceResolver resolver)
    {
        sourceResolver_ = std::move(resolver);
        declaredSourceResolver_ = {};
    }

    using DeclaredSourceResolver = core::SharedCallback<ModuleResolution(const core::pmr::String&)>;

    void setDeclaredSourceResolver(DeclaredSourceResolver resolver)
    {
        declaredSourceResolver_ = std::move(resolver);
        sourceResolver_ = {};
    }

    // Set an importer for explicit .arpplugin manifests. The registry does
    // not own the importer; it must outlive this registry.
    void setCppPluginImporter(CppPluginImporter* importer) { cppPluginImporter_ = importer; }

    void setStaticPluginCatalog(
        const StaticPluginCatalog* catalog, ScriptPluginServiceRegistry* services = nullptr)
    {
        staticCatalog_ = catalog;
        pluginServices_ = services;
    }

    void setPluginServices(ScriptPluginServiceRegistry* services) { pluginServices_ = services; }

    // Installs the result of a plugin entry for embedders that already own
    // the service scope (tests and statically linked applications). This is
    // not a host-module registration path; the export table must have been
    // produced by ScriptPluginRegistrar.
    bool installPluginModule(const core::pmr::String& name, std::shared_ptr<ModuleExports> exports)
    {
        if (name.empty() || !exports || exports->symbols.empty())
            return false;
        sourceCache_[name] = std::move(exports);
        return true;
    }

    // Load a module by name at runtime (called by the VM's LoadModule opcode).
    // Checks installed plugin/source cache, then source and plugin resolvers.
    // Caches results so each module is materialized at most once.
    // Returns nullptr if the module cannot be found.
    std::shared_ptr<ModuleExports> loadModule(const core::pmr::String& name);
    // Owner-thread cache observation only: never invokes a resolver/importer,
    // materializes a module, writes diagnostics or allocates an export owner.
    std::shared_ptr<ModuleExports> findLoadedModule(const core::pmr::String& name) const noexcept;

    // Query the list of exported symbol names for a module (for sema).
    // Returns an empty vector if the module is not found.
    core::pmr::Vector<core::pmr::String> queryExports(const core::pmr::String& name);

    // Check if a specific export exists in a module (for sema).
    bool hasExport(const core::pmr::String& moduleName, const core::pmr::String& exportName);

    // Returns authoring metadata only when a host export was explicitly
    // declared with HostCallableContract. Runtime Values are not inspected.
    const HostCallableContract* findHostCallableContract(
        const core::pmr::String& moduleName, const core::pmr::String& exportName);

    const ReflectionCallableContract* findReflectionCallableContract(
        const core::pmr::String& moduleName, const core::pmr::String& exportName);

    core::pmr::Vector<ReflectionCallableContract> queryReflectionCallableContracts(
        const core::pmr::String& moduleName);

    const arp::reflection::class_info* findType(
        const core::pmr::String& moduleName, const core::pmr::String& typeName);

    const arp::reflection::function* findFunction(
        const core::pmr::String& moduleName, const core::pmr::String& functionName);

    const ScriptFunctionContract* findScriptFunction(
        const core::pmr::String& moduleName, const core::pmr::String& functionName);

    // Resolve the immutable Plugin contract used by AOT before execution.
    bool resolveAotPluginDependency(
        const AotPluginDependencyV5& dependency, VmError* error = nullptr);

    // Returns the stable export value for sema/runtime capability checks.
    const Value* findExport(
        const core::pmr::String& moduleName, const core::pmr::String& exportName);

    const core::pmr::String& lastDiagnostics() const { return lastDiagnostics_; }

private:
    // Cache of loaded source modules (name -> exports).
    core::pmr::Map<core::pmr::String, std::shared_ptr<ModuleExports>> sourceCache_{
        &arp::core::defaultCpuMemoryResource()};

    // Modules currently being resolved (cycle detection).
    core::pmr::Vector<core::pmr::String> resolving_{&arp::core::defaultCpuMemoryResource()};

    SourceResolver sourceResolver_;
    DeclaredSourceResolver declaredSourceResolver_;
    CppPluginImporter* cppPluginImporter_ = nullptr;
    const StaticPluginCatalog* staticCatalog_ = nullptr;
    ScriptPluginServiceRegistry* pluginServices_ = nullptr;
    core::pmr::String lastDiagnostics_{&arp::core::defaultCpuMemoryResource()};
};

// Adapter for ScriptAotPackageRegistry's pre-execution dependency resolver.
ARP_API bool resolveAotPluginDependencyFromModuleRegistry(
    void* userData, const AotPluginDependencyV5& dependency, VmError* error);

// Register ModuleObject into the reflection db (idempotent).
void registerModuleReflection();

// Check whether a Value carries a C++ reflection type binding.
ARP_API bool isReflectedTypeBinding(const Value& v);

// Compatibility spelling for class bindings.
ARP_API bool isReflectedClassBinding(const Value& v);

// Extract the shared reflection type contract from a binding.
ARP_API const arp::reflection::class_info* asReflectedTypeBinding(const Value& v);

ARP_API bool isReflectedFunction(const Value& v);
ARP_API const arp::reflection::function* asReflectedFunction(const Value& v);

// Compatibility spelling for class bindings.
ARP_API const arp::reflection::class_info* asReflectedClassBinding(const Value& v);

// Check if a Value carries a ModuleObject.
bool isModuleObject(const Value& v);

// Extract the ModuleExports from a Value carrying a ModuleObject.
// Returns nullptr if v is not a ModuleObject.
std::shared_ptr<ModuleExports> asModuleObject(const Value& v);

} // namespace arp::scriptvm

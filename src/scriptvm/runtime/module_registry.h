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

#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/compile/ast.h"
#include "scriptvm/runtime/host.h"
#include "scriptvm/runtime/module_registration.h"
#include "scriptvm/runtime/value.h"

#include <functional>
#include <memory>
#include <utility>

struct ArpScriptPluginHostV2;

namespace arp::scriptvm {

class CppPluginImporter;

// Services are supplied by the embedding runtime and are deliberately
// addressed by a stable id/version pair. Plugins receive only the services
// they declare in their manifest; they do not reach back into ModuleRegistry
// or construct a second host-module abstraction.
struct ScriptPluginService {
    core::pmr::String serviceId{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t version = 0;
    void* value = nullptr;
};

struct ScriptPluginServiceRequirement {
    core::pmr::String serviceId{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t version = 0u;
};

class ARP_API ScriptPluginServiceRegistry final {
public:
    bool provide(core::pmr::String serviceId, std::uint32_t version, void* value);
    void clear();
    void* find(const core::pmr::String& serviceId, std::uint32_t minimumVersion = 1u) const;
    std::uint32_t versionOf(const core::pmr::String& serviceId) const;

    template <typename T>
    T* find(const core::pmr::String& serviceId, std::uint32_t minimumVersion = 1u) const
    {
        return static_cast<T*>(find(serviceId, minimumVersion));
    }

private:
    core::pmr::Vector<ScriptPluginService> services_{&arp::core::defaultCpuMemoryResource()};
};

// Source-module functions keep the exact types produced by sema. C++ host
// functions use reflection::function instead; this contract is only for
// ArpScript-defined exports and avoids manufacturing a second reflection ABI.
struct ScriptFunctionContract {
    core::pmr::Vector<TypePtr> params{&arp::core::defaultCpuMemoryResource()};
    TypePtr result;
    bool isVariadic = false;
    // Optional Native authoring metadata. The sema type contract above is
    // sufficient for Interpreter execution; AOT requires all fields below
    // and never infers them from the exported Value or function name.
    core::pmr::String bindingId{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String targetId{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String signatureHash{&arp::core::defaultCpuMemoryResource()};
    int fixedArity = -1;
    bool maySuspend = false;
    // Explicit source-module contract for a generator value that may cross
    // the DirectNative/Interpreter delegation boundary. This is never inferred
    // from the runtime Value or function name.
    bool generatorBridge = false;
    std::uint32_t lifetimeVersion = 0;

    bool nativeBindingValid() const
    {
        return !bindingId.empty() && !targetId.empty() && !signatureHash.empty() && fixedArity >= 0
            && !isVariadic && lifetimeVersion != 0u;
    }
};

// An immutable export table: name -> Value.
struct ModuleExports {
    core::pmr::Map<core::pmr::String, Value> symbols{&arp::core::defaultCpuMemoryResource()};
    // Version copied from the v2 plugin manifest. Dynamic and static loading
    // must expose the same module identity to AOT dependency validation.
    std::uint32_t moduleVersion = 0u;
    // Contract identity copied from the v2 manifest.  Dynamic and static
    // plugin paths expose the same value so AOT/runtime validation can reject
    // an artifact built against a different export surface.
    core::pmr::String exportContractHash{&arp::core::defaultCpuMemoryResource()};
    // Hash of the generated reflection registry used by AOT dependency
    // validation. Empty means the plugin predates the extended contract.
    core::pmr::String reflectionRegistryHash{&arp::core::defaultCpuMemoryResource()};
    core::pmr::Vector<ScriptPluginServiceRequirement> requiredServices{
        &arp::core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, HostCallableContract> hostCallableContracts{
        &arp::core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, ReflectionCallableContract> reflectionCallableContracts{
        &arp::core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, ScriptFunctionContract> scriptFunctionContracts{
        &arp::core::defaultCpuMemoryResource()};
    std::shared_ptr<ModuleRegistration> reflectionRegistration;
    // Keeps a source-built plugin and its reflected method code loaded while
    // any script module value remains reachable.
    std::shared_ptr<void> nativeLifetime;

    ModuleExports() = default;
};

// A HostCallable wraps a HostFn so it can be stored as a Value in a module's
// export table and invoked by the VM's CallValue opcode.
struct HostCallable {
    HostFn fn;
    HostCallableContract contract;

    explicit HostCallable(HostFn f) : fn(std::move(f)) {}

    HostCallable(HostFn f, HostCallableContract metadata)
        : fn(std::move(f)), contract(std::move(metadata))
    {}
};

// A ModuleObject wraps a SharedPtr<ModuleExports> so the VM can carry it
// inside a reflection::object (registered like ScriptCell / ScriptPointer).
struct ModuleObject {
    std::shared_ptr<ModuleExports> exports;

    explicit ModuleObject(std::shared_ptr<ModuleExports> e = {}) : exports(std::move(e)) {}
};

// A non-owning binding to a class already registered in the reflection DB.
// The script module and C++ host share the same class_info contract.
struct ReflectedTypeBinding {
    const arp::reflection::class_info* classInfo = nullptr;

    explicit ReflectedTypeBinding(const arp::reflection::class_info* info = nullptr)
        : classInfo(info)
    {}
};

using ReflectedClassBinding = ReflectedTypeBinding;

// Registrar for one C++ plugin module. The registrar owns the mutable export
// table during plugin entry and freezes it when build() is called. No plugin
// receives ModuleRegistry or an implementation-specific host-module builder.
class ARP_API ScriptPluginRegistrar final {
public:
    explicit ScriptPluginRegistrar(
        core::pmr::String name, ScriptPluginServiceRegistry* services = nullptr)
        : name_(std::move(name)), exports_(arp::core::makeShared<ModuleExports>()),
          reflectionModule_(arp::core::makeShared<ModuleRegistration>(name_)), services_(services)
    {
        exports_->reflectionRegistration = reflectionModule_;
    }

    // Export an arbitrary Value (function, object, scalar).
    bool exportValue(core::pmr::String name, Value v);

    // Retain native engine objects created during plugin registration for the
    // lifetime of the immutable export table. This is the only ownership
    // bridge for module-bound native objects; scripts receive reflection
    // references, never the owner itself.
    bool retainNativeLifetime(std::shared_ptr<void> lifetime);

    // Bind any existing C++ reflection type without creating a script-side
    // type mirror. Construction is available when the class_info has __new__;
    // static reflected fields and enum descriptors remain available otherwise.
    bool bindType(core::pmr::String name, const arp::reflection::class_info* classInfo);

    // Bind a codegen-produced enumeration as a module-level symbol. The
    // enumeration remains reflection metadata; script member access resolves
    // its named values through the VM's existing enumeration path.
    bool bindEnum(core::pmr::String name, arp::reflection::enumeration enumeration)
    {
        if (frozen_ || name.empty() || exports_->symbols.find(name) != exports_->symbols.end()) {
            return false;
        }
        return reflectionModule_->bindEnum(name, std::move(enumeration));
    }

    ModuleRegistration* reflectionModule() { return reflectionModule_.get(); }

    template <typename T> bool bindType(core::pmr::String name)
    {
        return bindType(std::move(name), arp::reflection::class_of<T>());
    }

    // Compatibility spelling for callers that specifically bind a class.
    bool bindClass(core::pmr::String name, const arp::reflection::class_info* classInfo);

    template <typename T> bool bindClass(core::pmr::String name)
    {
        return bindType<T>(std::move(name));
    }

    // Export a host function (wrapped as a Value via ScriptClosure-like
    // carrier; the VM calls it through CallValue).
    // For simplicity we store the HostFn in a HostCallable struct registered
    // into reflection, and the VM's CallValue dispatches it.
    bool exportCallable(core::pmr::String name, HostFn fn);

    // A metadata-bearing export is the only HostCallable authoring path that
    // can later qualify for a Native indirect-call contract.
    bool exportCallable(core::pmr::String name, HostFn fn, HostCallableContract contract);

    // Service-backed callables are the only hand-authored plugin exports.
    // They capture a host service instance and therefore cannot be emitted by
    // reflection codegen. Keeping a distinct spelling makes this exception
    // auditable in plugin entry points and enforceable by the build contract.
    bool exportServiceCallable(core::pmr::String name, HostFn fn)
    {
        return exportCallable(std::move(name), std::move(fn));
    }

    bool exportServiceCallable(core::pmr::String name, HostFn fn, HostCallableContract contract)
    {
        return exportCallable(std::move(name), std::move(fn), std::move(contract));
    }

    // Explicit alias retained for source plugins whose exported operation is
    // a dynamically typed callable. Both spellings use the same validation
    // and export-table path; there is no separate host-module API.
    bool exportFunction(core::pmr::String name, HostFn fn)
    {
        return exportCallable(std::move(name), std::move(fn));
    }

    bool exportFunction(core::pmr::String name, HostFn fn, HostCallableContract contract)
    {
        return exportCallable(std::move(name), std::move(fn), std::move(contract));
    }

    // Export a typed reflection function. Its parameter and return contracts
    // remain available to sema while the VM invokes the same reflected value.
    bool exportFunction(core::pmr::String name, arp::reflection::function fn);
    bool exportFunction(
        core::pmr::String name, arp::reflection::function fn, ReflectionCallableContract contract);

    bool exportServiceFunction(core::pmr::String name, arp::reflection::function fn)
    {
        return exportFunction(std::move(name), std::move(fn));
    }

    // Finalize and return the immutable export table.
    std::shared_ptr<ModuleExports> build();

    // Counts domain exports authored by the plugin entry. The two ABI
    // metadata symbols inserted by build() are deliberately excluded.
    std::size_t explicitDomainExportCount() const noexcept
    {
        return explicitExportCount_ + reflectionModule_->types().size()
            + reflectionModule_->functions().size() + reflectionModule_->enums().size()
            + reflectionModule_->objects().size();
    }

    const core::pmr::String& name() const { return name_; }

    ScriptPluginServiceRegistry* services() const { return services_; }

private:
    core::pmr::String name_;
    std::shared_ptr<ModuleExports> exports_;
    std::shared_ptr<ModuleRegistration> reflectionModule_;
    ScriptPluginServiceRegistry* services_ = nullptr;
    std::shared_ptr<void> nativeLifetime_;
    std::size_t explicitExportCount_ = 0u;
    bool frozen_ = false;
};

// Static plugins use the exact same entry contract as dynamically loaded
// plugins. The catalog owns no services; the embedding runtime supplies them
// when a module is materialized.
using ScriptPluginEntry = int (*)(::ArpScriptPluginHostV2*);

struct ScriptPluginDescriptor {
    core::pmr::String module{&arp::core::defaultCpuMemoryResource()};
    std::uint32_t contractVersion = 2u;
    std::uint32_t moduleVersion = 1u;
    ScriptPluginEntry entry = nullptr;
    core::pmr::String exportContractHash{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&arp::core::defaultCpuMemoryResource()};
    core::pmr::Vector<ScriptPluginServiceRequirement> requiredServices{
        &arp::core::defaultCpuMemoryResource()};
};

class ARP_API StaticPluginCatalog final {
public:
    bool add(ScriptPluginDescriptor descriptor);

    void clear() { plugins_.clear(); }

    std::shared_ptr<ModuleExports> load(const core::pmr::String& module,
        ScriptPluginServiceRegistry* services,
        core::pmr::String& diagnostics) const;

#if defined(ARP_WOCC_CONTRACT_TEST_HOOKS)
    // Host-only fault injection for candidate-world contract tests. This is
    // deliberately unavailable to the script ABI and has no production call
    // sites; it lets tests verify AOT contract rejection against the same
    // immutable descriptor metadata used by real catalogs.
    bool overrideContractForTesting(const core::pmr::String& module,
        const core::pmr::String& exportContractHash,
        const core::pmr::String& reflectionRegistryHash)
    {
        for (auto& descriptor : plugins_) {
            if (descriptor.module != module)
                continue;
            descriptor.exportContractHash = exportContractHash;
            descriptor.reflectionRegistryHash = reflectionRegistryHash;
            return true;
        }
        return false;
    }
#endif

private:
    core::pmr::Vector<ScriptPluginDescriptor> plugins_{&arp::core::defaultCpuMemoryResource()};
};

// Registry for plugin modules and source modules.
class ARP_API ModuleRegistry {
public:
    ModuleRegistry() = default;

    // Set the resolver callback for source (.arp) modules.
    // The resolver receives a module name and returns the module's exports.
    // If the module cannot be found, the resolver should return nullptr.
    using SourceResolver = std::function<std::shared_ptr<ModuleExports>(const core::pmr::String&)>;

    void setSourceResolver(SourceResolver resolver) { sourceResolver_ = std::move(resolver); }

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
    CppPluginImporter* cppPluginImporter_ = nullptr;
    const StaticPluginCatalog* staticCatalog_ = nullptr;
    ScriptPluginServiceRegistry* pluginServices_ = nullptr;
    core::pmr::String lastDiagnostics_{&arp::core::defaultCpuMemoryResource()};
};

// Adapter for ScriptAotPackageRegistry's pre-execution dependency resolver.
ARP_API bool resolveAotPluginDependencyFromModuleRegistry(
    void* userData, const AotPluginDependencyV5& dependency, VmError* error);

/// Owns the per-world script registry/service scope. A reload creates a fresh
/// scope and generation; callers never migrate script instances or awaitables
/// from the previous generation.
class ARP_API ScriptPluginRuntimeScope final {
public:
    using ReloadBuilder = std::function<bool(ModuleRegistry&,
        ScriptPluginServiceRegistry&,
        StaticPluginCatalog&,
        std::uint64_t,
        core::pmr::String&)>;

    explicit ScriptPluginRuntimeScope(std::uint64_t generation = 1u)
        : generation_(generation == 0u ? 1u : generation)
    {}

    ModuleRegistry& modules() { return modules_; }

    ScriptPluginServiceRegistry& services() { return services_; }

    StaticPluginCatalog& staticCatalog() { return staticCatalog_; }

    std::uint64_t generation() const { return generation_; }

    // Build a replacement registry/service scope off to the side. The
    // current scope remains fully usable until builder returns true; failed
    // candidates are discarded without changing generation or services.
    bool reload(const ReloadBuilder& builder, core::pmr::String& diagnostics);

    void beginReload()
    {
        services_.clear();
        staticCatalog_.clear();
        modules_ = ModuleRegistry{};
        modules_.setStaticPluginCatalog(&staticCatalog_, &services_);
        ++generation_;
        if (generation_ == 0)
            generation_ = 1;
    }

private:
    ModuleRegistry modules_;
    ScriptPluginServiceRegistry services_;
    StaticPluginCatalog staticCatalog_;
    std::uint64_t generation_ = 1;
};

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

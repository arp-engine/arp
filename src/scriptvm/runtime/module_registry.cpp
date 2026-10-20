#include "scriptvm/runtime/module_registry.h"

#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/runtime/cpp_plugin_api.h"
#include "scriptvm/runtime/cpp_plugin_importer.h"

#include <cctype>
#include <string>

namespace arp::scriptvm {

namespace {

bool isPluginModuleIdentifier(const core::pmr::String& name)
{
    if (name.empty()
        || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) {
        return false;
    }
    for (const char character : name) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_')) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ScriptPluginServiceRegistry::provide(
    core::pmr::String serviceId, std::uint32_t version, void* value)
{
    if (serviceId.empty() || version == 0u || value == nullptr)
        return false;
    for (auto& service : services_) {
        if (service.serviceId == serviceId) {
            if (service.version != version)
                return false;
            service.value = value;
            return true;
        }
    }
    services_.push_back(ScriptPluginService{std::move(serviceId), version, value});
    return true;
}

void ScriptPluginServiceRegistry::clear()
{
    services_.clear();
}

bool ScriptPluginRuntimeScope::reload(const ReloadBuilder& builder, core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (!builder) {
        diagnostics = "script runtime reload builder is empty";
        return false;
    }

    ModuleRegistry candidateModules;
    ScriptPluginServiceRegistry candidateServices;
    StaticPluginCatalog candidateCatalog;
    candidateModules.setStaticPluginCatalog(&candidateCatalog, &candidateServices);

    auto candidateGeneration = generation_ + 1u;
    if (candidateGeneration == 0u)
        candidateGeneration = 1u;
    if (!builder(candidateModules,
            candidateServices,
            candidateCatalog,
            candidateGeneration,
            diagnostics)) {
        if (diagnostics.empty())
            diagnostics = "script runtime reload candidate was rejected";
        return false;
    }

    services_ = std::move(candidateServices);
    staticCatalog_ = std::move(candidateCatalog);
    modules_ = std::move(candidateModules);
    generation_ = candidateGeneration;
    // candidateModules pointed at its own members; repair those pointers
    // after committing the moved containers.
    modules_.setStaticPluginCatalog(&staticCatalog_, &services_);
    return true;
}

void* ScriptPluginServiceRegistry::find(
    const core::pmr::String& serviceId, std::uint32_t minimumVersion) const
{
    for (const auto& service : services_) {
        if (service.serviceId == serviceId && service.version >= minimumVersion)
            return service.value;
    }
    return nullptr;
}

std::uint32_t ScriptPluginServiceRegistry::versionOf(const core::pmr::String& serviceId) const
{
    for (const auto& service : services_) {
        if (service.serviceId == serviceId)
            return service.version;
    }
    return 0u;
}

bool ScriptPluginRegistrar::exportValue(core::pmr::String name, Value value)
{
    if (frozen_ || name.empty() || exports_->symbols.find(name) != exports_->symbols.end())
        return false;
    exports_->symbols[std::move(name)] = std::move(value);
    ++explicitExportCount_;
    return true;
}

bool ScriptPluginRegistrar::retainNativeLifetime(std::shared_ptr<void> lifetime)
{
    if (frozen_ || !lifetime || nativeLifetime_) {
        return false;
    }
    nativeLifetime_ = std::move(lifetime);
    return true;
}

bool ScriptPluginRegistrar::exportCallable(core::pmr::String name, HostFn fn)
{
    if (frozen_ || name.empty() || !fn)
        return false;
    HostCallable callable(std::move(fn));
    Value v{callable};
    return exportValue(std::move(name), std::move(v));
}

bool ScriptPluginRegistrar::exportCallable(
    core::pmr::String name, HostFn fn, HostCallableContract contract)
{
    if (frozen_ || name.empty() || !fn || !contract.valid()
        || exports_->symbols.find(name) != exports_->symbols.end())
        return false;
    HostCallable callable(std::move(fn), std::move(contract));
    exports_->hostCallableContracts[name] = callable.contract;
    exports_->symbols[std::move(name)] = Value{std::move(callable)};
    ++explicitExportCount_;
    return true;
}

bool ScriptPluginRegistrar::exportFunction(core::pmr::String name, arp::reflection::function fn)
{
    if (frozen_ || name.empty() || fn.name().empty()
        || exports_->symbols.find(name) != exports_->symbols.end())
        return false;
    exports_->symbols[std::move(name)] = Value{std::move(fn)};
    ++explicitExportCount_;
    return true;
}

bool ScriptPluginRegistrar::exportFunction(
    core::pmr::String name, arp::reflection::function fn, ReflectionCallableContract contract)
{
    if (frozen_ || name.empty() || fn.name().empty() || !contract.valid()
        || contract.functionIdentity != fn.identity()
        || exports_->symbols.find(name) != exports_->symbols.end()) {
        return false;
    }
    core::pmr::String expectedTarget(name_, &core::defaultCpuMemoryResource());
    expectedTarget += ".";
    expectedTarget += name;
    if (contract.targetId != expectedTarget)
        return false;
    for (const auto& entry : exports_->reflectionCallableContracts) {
        if (entry.second.bindingId == contract.bindingId)
            return false;
    }
    exports_->reflectionCallableContracts[name] = std::move(contract);
    exports_->symbols[std::move(name)] = Value{std::move(fn)};
    ++explicitExportCount_;
    return true;
}

bool ScriptPluginRegistrar::bindType(
    core::pmr::String name, const arp::reflection::class_info* classInfo)
{
    if (frozen_ || name.empty() || classInfo == nullptr
        || exports_->symbols.find(name) != exports_->symbols.end()) {
        return false;
    }
    if (!reflectionModule_->bindType(name, classInfo)) {
        return false;
    }
    exports_->symbols[std::move(name)] = Value{ReflectedTypeBinding{classInfo}};
    ++explicitExportCount_;
    return true;
}

bool ScriptPluginRegistrar::bindClass(
    core::pmr::String name, const arp::reflection::class_info* classInfo)
{
    return bindType(std::move(name), classInfo);
}

std::shared_ptr<ModuleExports> ScriptPluginRegistrar::build()
{
    if (frozen_)
        return nullptr;

    // These two exports are ABI metadata, not domain API. Generate them at
    // the registrar boundary so plugin entries never hand-author ordinary
    // values alongside their codegen-backed reflection exports.
    const auto explicitCount = explicitExportCount_;
    if (!exportValue(
            core::pmr::String("abi_version", &core::defaultCpuMemoryResource()), makeInt(2))
        || !exportValue(core::pmr::String("domain", &core::defaultCpuMemoryResource()),
            Value{core::pmr::String(name_, &core::defaultCpuMemoryResource())})) {
        return nullptr;
    }
    explicitExportCount_ = explicitCount;
    for (const auto& object : reflectionModule_->objects()) {
        if (!exportValue(
                core::pmr::String(object.name, &core::defaultCpuMemoryResource()), object.value)) {
            return nullptr;
        }
    }
    frozen_ = true;
    exports_->nativeLifetime = std::move(nativeLifetime_);
    for (const auto& type : reflectionModule_->types()) {
        if (exports_->symbols.find(type.name) == exports_->symbols.end()) {
            exports_->symbols[type.name] = Value{ReflectedTypeBinding{type.classInfo}};
        }
    }
    for (const auto& function : reflectionModule_->functions()) {
        if (exports_->symbols.find(function.name) == exports_->symbols.end()) {
            exports_->symbols[function.name] = Value{function.function};
        }
    }
    for (const auto& callable : reflectionModule_->reflectionCallables()) {
        if (exports_->reflectionCallableContracts.find(callable.key)
            != exports_->reflectionCallableContracts.end()) {
            return nullptr;
        }
        exports_->reflectionCallableContracts[callable.key] = callable.contract;
    }
    for (const auto& enumeration : reflectionModule_->enums()) {
        if (exports_->symbols.find(enumeration.name) != exports_->symbols.end())
            return nullptr;
        exports_->symbols[enumeration.name] = Value{enumeration.enumeration};
    }
    return std::move(exports_);
}

bool StaticPluginCatalog::add(ScriptPluginDescriptor descriptor)
{
    if (!isPluginModuleIdentifier(descriptor.module) || descriptor.contractVersion != 2u
        || descriptor.moduleVersion == 0u || descriptor.exportContractHash.empty()
        || descriptor.entry == nullptr)
        return false;
    for (const auto& existing : plugins_) {
        if (existing.module == descriptor.module)
            return false;
    }
    plugins_.push_back(std::move(descriptor));
    return true;
}

std::shared_ptr<ModuleExports> StaticPluginCatalog::load(const core::pmr::String& module,
    ScriptPluginServiceRegistry* services,
    core::pmr::String& diagnostics) const
{
    for (const auto& descriptor : plugins_) {
        if (descriptor.module != module)
            continue;
        // Dynamic and static loading must reject the same service contract
        // before executing the plugin entry. Loading an export table with a
        // missing or older service would otherwise make the two paths
        // observably different and could leave a partially registered module.
        for (const auto& required : descriptor.requiredServices) {
            if (services == nullptr
                || services->find(required.serviceId, required.version) == nullptr) {
                diagnostics = "static C++ plugin requires unavailable service ";
                diagnostics += required.serviceId;
                diagnostics += "/v";
                diagnostics += std::to_string(required.version).c_str();
                return nullptr;
            }
        }
        ScriptPluginRegistrar registrar(
            core::pmr::String(module, &core::defaultCpuMemoryResource()), services);
        ArpScriptPluginHostV2 host{2u,
            &registrar,
            services,
#if defined(__EMSCRIPTEN__)
            "emscripten",
#elif defined(_WIN32)
            "windows",
#else
            "native",
#endif
#if defined(NDEBUG)
            "release"
#else
            "debug"
#endif
            ,
            "script,reflection,services"};
        if (descriptor.entry(&host) == 0) {
            diagnostics = "static C++ plugin rejected module: ";
            diagnostics += module;
            return nullptr;
        }
        auto exports = registrar.build();
        if (!exports || exports->symbols.empty()) {
            diagnostics = "static C++ plugin exported no symbols: ";
            diagnostics += module;
            return nullptr;
        }
        exports->moduleVersion = descriptor.moduleVersion;
        exports->exportContractHash = descriptor.exportContractHash;
        exports->reflectionRegistryHash = descriptor.reflectionRegistryHash;
        exports->requiredServices = descriptor.requiredServices;
        return exports;
    }
    return nullptr;
}

// --- ModuleRegistry ---

std::shared_ptr<ModuleExports> ModuleRegistry::loadModule(const core::pmr::String& name)
{
    lastDiagnostics_.clear();
    // Check source cache.
    auto cacheIt = sourceCache_.find(name);
    if (cacheIt != sourceCache_.end())
        return cacheIt->second;

    // Cycle detection.
    for (const auto& r : resolving_) {
        if (r == name) {
            // Circular import.
            lastDiagnostics_ = "circular module dependency: ";
            lastDiagnostics_ += name;
            return nullptr;
        }
    }

    // Resolve via source resolver first, then the explicit C++ plugin importer.
    std::shared_ptr<ModuleExports> exports;
    if (sourceResolver_) {
        resolving_.push_back(name);
        try {
            exports = sourceResolver_(name);
        } catch (const std::bad_alloc&) {
            resolving_.pop_back();
            throw;
        } catch (...) {
            lastDiagnostics_ = "source module resolver threw";
            exports.reset();
        }
        resolving_.pop_back();
    }

    if (exports)
        sourceCache_[name] = exports;

    if (exports)
        return exports;

    if (cppPluginImporter_ != nullptr) {
        exports = cppPluginImporter_->loadModule(name);
        if (exports)
            sourceCache_[name] = exports;
        else
            lastDiagnostics_ = cppPluginImporter_->lastDiagnostics();
    }

    if (staticCatalog_ != nullptr) {
        exports = staticCatalog_->load(name, pluginServices_, lastDiagnostics_);
        if (exports)
            sourceCache_[name] = exports;
        if (exports)
            return exports;
    }

    return exports;
}

core::pmr::Vector<core::pmr::String> ModuleRegistry::queryExports(const core::pmr::String& name)
{
    core::pmr::Vector<core::pmr::String> result{&arp::core::defaultCpuMemoryResource()};

    auto exports = loadModule(name);
    if (!exports)
        return result;

    for (const auto& [key, _] : exports->symbols)
        result.push_back(key);

    return result;
}

bool ModuleRegistry::hasExport(
    const core::pmr::String& moduleName, const core::pmr::String& exportName)
{
    auto exports = loadModule(moduleName);
    if (!exports)
        return false;
    return exports->symbols.find(exportName) != exports->symbols.end();
}

const HostCallableContract* ModuleRegistry::findHostCallableContract(
    const core::pmr::String& moduleName, const core::pmr::String& exportName)
{
    auto exports = loadModule(moduleName);
    if (!exports)
        return nullptr;
    const auto found = exports->hostCallableContracts.find(exportName);
    return found == exports->hostCallableContracts.end() ? nullptr : &found->second;
}

const ReflectionCallableContract* ModuleRegistry::findReflectionCallableContract(
    const core::pmr::String& moduleName, const core::pmr::String& exportName)
{
    auto exports = loadModule(moduleName);
    if (!exports)
        return nullptr;
    const auto found = exports->reflectionCallableContracts.find(exportName);
    if (found != exports->reflectionCallableContracts.end())
        return &found->second;
    core::pmr::String target(moduleName, &core::defaultCpuMemoryResource());
    target += ".";
    target += exportName;
    for (const auto& entry : exports->reflectionCallableContracts) {
        if (entry.second.targetId == target)
            return &entry.second;
    }
    return nullptr;
}

core::pmr::Vector<ReflectionCallableContract> ModuleRegistry::queryReflectionCallableContracts(
    const core::pmr::String& moduleName)
{
    core::pmr::Vector<ReflectionCallableContract> result{&core::defaultCpuMemoryResource()};
    const auto exports = loadModule(moduleName);
    if (!exports)
        return result;
    result.reserve(exports->reflectionCallableContracts.size());
    for (const auto& entry : exports->reflectionCallableContracts)
        result.push_back(entry.second);
    return result;
}

const arp::reflection::class_info* ModuleRegistry::findType(
    const core::pmr::String& moduleName, const core::pmr::String& typeName)
{
    const auto exports = loadModule(moduleName);
    if (!exports) {
        return nullptr;
    }
    const auto found = exports->symbols.find(typeName);
    return found == exports->symbols.end() ? nullptr : asReflectedTypeBinding(found->second);
}

const arp::reflection::function* ModuleRegistry::findFunction(
    const core::pmr::String& moduleName, const core::pmr::String& functionName)
{
    const auto exports = loadModule(moduleName);
    if (!exports) {
        return nullptr;
    }
    const auto found = exports->symbols.find(functionName);
    if (found != exports->symbols.end()) {
        if (const auto* reflected = asReflectedFunction(found->second)) {
            return reflected;
        }
    }
    return nullptr;
}

const ScriptFunctionContract* ModuleRegistry::findScriptFunction(
    const core::pmr::String& moduleName, const core::pmr::String& functionName)
{
    const auto exports = loadModule(moduleName);
    if (!exports) {
        return nullptr;
    }
    const auto found = exports->scriptFunctionContracts.find(functionName);
    return found == exports->scriptFunctionContracts.end() ? nullptr : &found->second;
}

bool ModuleRegistry::resolveAotPluginDependency(
    const AotPluginDependencyV5& dependency, VmError* error)
{
    const auto fail = [error](const char* message) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotModuleUnavailable;
            error->message = message;
        }
        return false;
    };
    if (dependency.moduleId == nullptr || dependency.moduleVersion == 0u
        || dependency.exportContractHash == nullptr
        || dependency.reflectionRegistryHash == nullptr) {
        return fail("AOT Plugin dependency identity is incomplete");
    }
    core::pmr::String moduleName(dependency.moduleId, &arp::core::defaultCpuMemoryResource());
    const auto exports = loadModule(moduleName);
    if (!exports) {
        return fail("AOT Plugin dependency is not available");
    }
    if (exports->moduleVersion != dependency.moduleVersion
        || exports->exportContractHash
            != core::pmr::String(
                dependency.exportContractHash, &arp::core::defaultCpuMemoryResource())
        || exports->reflectionRegistryHash
            != core::pmr::String(
                dependency.reflectionRegistryHash, &arp::core::defaultCpuMemoryResource())) {
        return fail("AOT Plugin dependency contract hash or version mismatches runtime");
    }
    for (std::size_t index = 0; index < dependency.serviceCount; ++index) {
        const auto& expected = dependency.services[index];
        bool declared = false;
        for (const auto& actual : exports->requiredServices) {
            if (actual.serviceId == expected.serviceId && actual.version == expected.version) {
                declared = true;
                break;
            }
        }
        if (!declared) {
            return fail("AOT Plugin service dependency mismatches runtime manifest");
        }
        if (pluginServices_ != nullptr) {
            core::pmr::String serviceId(expected.serviceId, &arp::core::defaultCpuMemoryResource());
            if (pluginServices_->versionOf(serviceId) < expected.version) {
                return fail("AOT Plugin service dependency is unavailable at runtime");
            }
        }
    }
    return true;
}

bool resolveAotPluginDependencyFromModuleRegistry(
    void* userData, const AotPluginDependencyV5& dependency, VmError* error)
{
    return userData != nullptr
        && static_cast<ModuleRegistry*>(userData)->resolveAotPluginDependency(dependency, error);
}

const Value* ModuleRegistry::findExport(
    const core::pmr::String& moduleName, const core::pmr::String& exportName)
{
    const auto exports = loadModule(moduleName);
    if (!exports)
        return nullptr;
    const auto found = exports->symbols.find(exportName);
    return found == exports->symbols.end() ? nullptr : &found->second;
}

// --- Reflection registration ---

void registerModuleReflection()
{
    (void)arp::reflection::registration::class_<HostCallable>("ArpScriptHostCallable");
    (void)arp::reflection::registration::class_<ModuleObject>("ArpScriptModuleObject");
    (void)arp::reflection::registration::class_<ReflectedTypeBinding>(
        "ArpScriptReflectedTypeBinding");
}

bool isReflectedTypeBinding(const Value& v)
{
    return v.is<ReflectedTypeBinding>();
}

bool isReflectedClassBinding(const Value& v)
{
    return isReflectedTypeBinding(v);
}

const arp::reflection::class_info* asReflectedTypeBinding(const Value& v)
{
    if (!isReflectedTypeBinding(v)) {
        return nullptr;
    }
    return v.ptr<ReflectedTypeBinding>()->classInfo;
}

bool isReflectedFunction(const Value& v)
{
    return v.is<arp::reflection::function>();
}

const arp::reflection::function* asReflectedFunction(const Value& v)
{
    return isReflectedFunction(v) ? v.ptr<arp::reflection::function>() : nullptr;
}

const arp::reflection::class_info* asReflectedClassBinding(const Value& v)
{
    return asReflectedTypeBinding(v);
}

bool isModuleObject(const Value& v)
{
    return v.is<ModuleObject>();
}

std::shared_ptr<ModuleExports> asModuleObject(const Value& v)
{
    if (!v.is<ModuleObject>())
        return nullptr;
    return v.ptr<ModuleObject>()->exports;
}

} // namespace arp::scriptvm

#include "scriptvm/runtime/module_registry.h"

#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/plugin/cpp_plugin_api.h"
#include "scriptvm/plugin/cpp_plugin_importer.h"

#include <cctype>
#include <string>

namespace arp::scriptvm {

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

#include "scriptvm/plugin/plugin_registrar.h"

namespace arp::scriptvm {

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

} // namespace arp::scriptvm

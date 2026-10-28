#include "scriptvm/runtime/module_registration.h"

#include "reflection/registration_transaction.h"

#include <utility>

namespace arp::scriptvm {

ModuleRegistration::ModuleRegistration(arp::reflection::NameView name)
    : name_(name.data(), name.size(), &core::defaultCpuMemoryResource())
{}

bool ModuleRegistration::bindType(
    arp::reflection::NameView name, const arp::reflection::class_info* classInfo)
{
    if (name.empty() || classInfo == nullptr) {
        return false;
    }
    for (const auto& entry : types_) {
        if (entry.name == name) {
            return entry.classInfo == classInfo;
        }
    }
    types_.push_back(ModuleTypeBinding{
        core::pmr::String(name.data(), name.size(), &core::defaultCpuMemoryResource()),
        classInfo,
    });
    return true;
}

bool ModuleRegistration::exportFunction(
    arp::reflection::NameView name, arp::reflection::function function)
{
    if (name.empty()) {
        return false;
    }
    for (const auto& entry : functions_) {
        if (entry.name == name) {
            return false;
        }
    }
    functions_.push_back(ModuleFunctionBinding{
        core::pmr::String(name.data(), name.size(), &core::defaultCpuMemoryResource()),
        std::move(function),
    });
    return true;
}

bool ModuleRegistration::exportFunction(arp::reflection::NameView name,
    arp::reflection::function function,
    ReflectionCallableContract contract)
{
    if (!contract.valid() || contract.functionIdentity != function.identity())
        return false;
    const auto callableKey = contract.targetId;
    if (!exportFunction(name, function))
        return false;
    return registerReflectionCallable(callableKey, std::move(contract));
}

bool ModuleRegistration::registerReflectionCallable(
    arp::reflection::NameView key, ReflectionCallableContract contract)
{
    if (key.empty() || !contract.valid())
        return false;
    for (const auto& entry : reflectionCallables_) {
        if (entry.key == key || entry.contract.bindingId == contract.bindingId
            || entry.contract.targetId == contract.targetId) {
            return entry.key == key && entry.contract.bindingId == contract.bindingId
                && entry.contract.targetId == contract.targetId
                && entry.contract.signatureHash == contract.signatureHash;
        }
    }
    reflectionCallables_.push_back(ModuleReflectionCallableBinding{
        core::pmr::String(key.data(), key.size(), &core::defaultCpuMemoryResource()),
        std::move(contract)});
    return true;
}

bool ModuleRegistration::bindObject(arp::reflection::NameView name, arp::reflection::object value)
{
    if (name.empty() || value.is_undefined()) {
        return false;
    }
    for (const auto& entry : objects_) {
        if (entry.name == name) {
            return false;
        }
    }
    for (const auto& entry : types_) {
        if (entry.name == name)
            return false;
    }
    for (const auto& entry : functions_) {
        if (entry.name == name)
            return false;
    }
    for (const auto& entry : enums_) {
        if (entry.name == name)
            return false;
    }
    objects_.push_back(ModuleObjectBinding{
        core::pmr::String(name.data(), name.size(), &core::defaultCpuMemoryResource()),
        std::move(value),
    });
    return true;
}

bool ModuleRegistration::bindEnum(
    arp::reflection::NameView name, arp::reflection::enumeration enumeration)
{
    if (name.empty() || enumeration.name().empty()) {
        return false;
    }
    for (const auto& entry : enums_) {
        if (entry.name == name) {
            return entry.enumeration.name() == enumeration.name();
        }
    }
    for (const auto& entry : types_) {
        if (entry.name == name)
            return false;
    }
    for (const auto& entry : functions_) {
        if (entry.name == name)
            return false;
    }
    enums_.push_back(ModuleEnumBinding{
        core::pmr::String(name.data(), name.size(), &core::defaultCpuMemoryResource()),
        std::move(enumeration),
    });
    return true;
}

const core::pmr::String& ModuleRegistration::name() const
{
    return name_;
}

const core::pmr::Vector<ModuleTypeBinding>& ModuleRegistration::types() const
{
    return types_;
}

const core::pmr::Vector<ModuleFunctionBinding>& ModuleRegistration::functions() const
{
    return functions_;
}

const core::pmr::Vector<ModuleReflectionCallableBinding>&
ModuleRegistration::reflectionCallables() const
{
    return reflectionCallables_;
}

const core::pmr::Vector<ModuleEnumBinding>& ModuleRegistration::enums() const
{
    return enums_;
}

const core::pmr::Vector<ModuleObjectBinding>& ModuleRegistration::objects() const
{
    return objects_;
}

bool ModuleRegistration::remapReflectionTypes(
    const arp::reflection::RegistrationTransaction& transaction)
{
    for (auto& binding : types_) {
        binding.classInfo = transaction.publicationClass(binding.classInfo);
    }
    for (auto& binding : reflectionCallables_) {
        binding.contract.functionIdentity =
            transaction.publicationFunctionIdentity(binding.contract.functionIdentity);
        if (!binding.contract.valid())
            return false;
    }
    return true;
}

} // namespace arp::scriptvm

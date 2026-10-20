#pragma once

#include "scene/script_behavior_binding.h"
#include "scene/script_behavior_component.h"
#include "scriptvm/aot/aot_v5.h"

#include <memory>
#include <utility>

namespace arp::scriptvm {

inline bool describeDirectNativeSceneBehaviorValue(
    void*, const Value& value, ScriptValueMemoryDescriptor& descriptor) noexcept
{
    if (!value.is<arp::scene::SceneUpdateContext>())
        return false;
    descriptor.allocationIdentity = value.storageIdentity();
    descriptor.retainedBytes = 0u;
    descriptor.externallyLeased = true;
    return descriptor.allocationIdentity != nullptr;
}

using DirectNativeScriptBehaviorFactoryV5 =
    std::function<arp::scene::ScriptBehaviorComponent::Invoke(
        const arp::scene::ScriptBehaviorBinding&)>;

inline arp::scene::ScriptBehaviorComponent::Invoke makeDirectNativeScriptBehaviorInvokeV5(
    std::shared_ptr<DirectNativeDispatcherV5> dispatcher,
    core::pmr::String moduleId,
    Value receiver = {},
    core::pmr::String functionPrefix = {})
{
    if (dispatcher != nullptr) {
        dispatcher->setValueMemoryDescriptor(&describeDirectNativeSceneBehaviorValue, nullptr);
    }
    const bool hasReceiver = !isNil(receiver);
    return [dispatcher = std::move(dispatcher),
               moduleId = std::move(moduleId),
               receiver = std::move(receiver),
               hasReceiver,
               prefix = std::move(functionPrefix)](
               const char* hook, const arp::scene::SceneUpdateContext& context) {
        if (dispatcher == nullptr || moduleId.empty() || hook == nullptr)
            return false;

        core::pmr::String functionId("arp-fn:", &core::defaultCpuMemoryResource());
        functionId += moduleId;
        functionId += "/";
        if (!prefix.empty()) {
            functionId += prefix;
            functionId += ".";
        }
        functionId += hook;
        if (!dispatcher->hasFunction(functionId.c_str()))
            return true;

        core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
        if (hasReceiver)
            args.emplace_back(receiver);
        args.emplace_back(Value{context});
        VmError error;
        const auto result =
            dispatcher->invoke(functionId.c_str(), args.data(), args.size(), &error);
        if (error.code != ScriptDiagnosticCode::None)
            return false;

        bool scriptResult = true;
        return isNil(result) || !tryGetBool(result, scriptResult) || scriptResult;
    };
}

// Construct the scene adapter callback from the registered v5 package. The
// registry remains the owner of the package identity; each binding receives a
// fresh module instance and dispatcher, so scene instances cannot share
// mutable execution state accidentally.
inline DirectNativeScriptBehaviorFactoryV5 makeDirectNativeScriptBehaviorFactoryV5(
    const ScriptAotPackageRegistry& registry, const char* packageIdentity, const char* moduleId)
{
    if (packageIdentity == nullptr || moduleId == nullptr || packageIdentity[0] == '\0'
        || moduleId[0] == '\0') {
        return {};
    }
    const core::pmr::String package(packageIdentity, &core::defaultCpuMemoryResource());
    const core::pmr::String module(moduleId, &core::defaultCpuMemoryResource());
    return [&registry, package, module](const arp::scene::ScriptBehaviorBinding& binding) {
        ScriptAotModuleInstanceV5 instance;
        const auto created =
            createAotOnlyModuleInstanceV5(registry, package.c_str(), module.c_str(), instance);
        if (!created.ok())
            return arp::scene::ScriptBehaviorComponent::Invoke{};
        auto dispatcher = core::makeShared<DirectNativeDispatcherV5>(std::move(instance));
        return makeDirectNativeScriptBehaviorInvokeV5(
            std::move(dispatcher), module, {}, binding.functionPrefix);
    };
}

} // namespace arp::scriptvm

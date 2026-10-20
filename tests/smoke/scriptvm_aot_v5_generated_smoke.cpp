#include "core/cpu_allocator.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/value.h"

#include <cstddef>
#include <cstring>
#include <utility>

extern "C" int arp_register_script_aot_package_v5(arp::scriptvm::AotRegistrationHostV5* host);

namespace {

const arp::scriptvm::AotPackageDescriptorV5* registeredPackage = nullptr;

struct BoundaryState {
    bool suspended = false;
    std::uint32_t visits[4]{};
};

struct NestedBoundaryState {
    bool suspended = false;
    std::uint32_t addVisits[4]{};
    std::uint32_t callerVisits[6]{};
};

struct SideEffectBoundaryState {
    bool suspended = false;
    std::uint32_t visits[9]{};
};

bool capturePackage(const arp::scriptvm::AotPackageDescriptorV5* package, void*)
{
    registeredPackage = package;
    return true;
}

bool suspendAddOnce(void* userData,
    const char*,
    std::uint32_t pc,
    arp::scriptvm::DirectNativeResumeStateV5&,
    arp::scriptvm::VmError*)
{
    auto& state = *static_cast<BoundaryState*>(userData);
    if (pc < 4u)
        ++state.visits[pc];
    if (!state.suspended && pc == 2u) {
        state.suspended = true;
        return false;
    }
    return true;
}

bool suspendNestedAddOnce(void* userData,
    const char* functionId,
    std::uint32_t pc,
    arp::scriptvm::DirectNativeResumeStateV5&,
    arp::scriptvm::VmError*)
{
    auto& state = *static_cast<NestedBoundaryState*>(userData);
    const bool add = functionId != nullptr && std::strcmp(functionId, "arp-fn:basic/add") == 0;
    if (add && pc < 4u)
        ++state.addVisits[pc];
    if (!add && pc < 6u)
        ++state.callerVisits[pc];
    if (add && !state.suspended && pc == 2u) {
        state.suspended = true;
        return false;
    }
    return true;
}

bool suspendAfterAppend(void* userData,
    const char* functionId,
    std::uint32_t pc,
    arp::scriptvm::DirectNativeResumeStateV5&,
    arp::scriptvm::VmError*)
{
    auto& state = *static_cast<SideEffectBoundaryState*>(userData);
    if (functionId == nullptr || std::strcmp(functionId, "arp-fn:basic/append_then_length") != 0)
        return true;
    if (pc < 9u)
        ++state.visits[pc];
    if (!state.suspended && pc == 5u) {
        state.suspended = true;
        return false;
    }
    return true;
}

bool invokeReflectionFixture(void*,
    const char*,
    const char* targetId,
    const arp::scriptvm::Value* receiver,
    const arp::scriptvm::Value* arguments,
    std::size_t argumentCount,
    arp::scriptvm::Value* result,
    arp::scriptvm::VmError* error)
{
    int receiverValue = 0;
    int argumentValue = 0;
    if (targetId == nullptr || std::strcmp(targetId, "host.Box.get") != 0 || receiver == nullptr
        || argumentCount != 1u || arguments == nullptr
        || !arp::scriptvm::tryGetInt(*receiver, receiverValue)
        || !arp::scriptvm::tryGetInt(arguments[0], argumentValue)) {
        if (error != nullptr) {
            error->code = arp::scriptvm::ScriptDiagnosticCode::HostError;
            error->message = "reflection fixture ABI mismatch";
        }
        return false;
    }
    *result = arp::scriptvm::makeInt(receiverValue + argumentValue);
    return true;
}

} // namespace

int main()
{
    using namespace arp::scriptvm;
    AotRegistrationHostV5 host{kAotContractVersion, &capturePackage, nullptr};
    if (arp_register_script_aot_package_v5(&host) != 1 || registeredPackage == nullptr
        || registeredPackage->moduleCount != 2u)
        return 1;
    const auto& module = registeredPackage->modules[0];
    if (!validateAotPackageV5(*registeredPackage, currentAotPlatform()).ok()
        || module.functionCount == 0u
        || module.functions[0].executionKind != AotFunctionExecutionKind::DirectNative)
        return 2;
    DirectNativeExecutionContext context;
    context.memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    context.resetBudget(module.budget);
    const Value args[] = {makeInt(2), makeInt(3)};
    VmError error;
    const auto result = module.functions[0].entry(context, args, 2u, nullptr, &error);
    int value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(result, value) || value != 5)
        return 3;

    BoundaryState boundary;
    DirectNativeResumeStateV5 resume;
    context.resetBudget(module.budget);
    context.operationBoundary = &suspendAddOnce;
    context.operationBoundaryUserData = &boundary;
    error = {};
    const auto suspended = module.functions[0].entry(context, args, 2u, &resume, &error);
    (void)suspended;
    if (error.code != ScriptDiagnosticCode::Suspended || resume.frame == nullptr || resume.pc != 2u
        || boundary.visits[0] != 1u || boundary.visits[1] != 1u || boundary.visits[2] != 1u)
        return 4;

    error = {};
    const auto resumed = module.functions[0].entry(context, nullptr, 0u, &resume, &error);
    value = 0;
    if (!(error.code == ScriptDiagnosticCode::None && tryGetInt(resumed, value) && value == 5
            && resume.frame == nullptr && resume.pc == 0u && resume.commitState == 0u
            && boundary.visits[0] == 1u && boundary.visits[1] == 1u && boundary.visits[2] == 2u
            && boundary.visits[3] == 1u))
        return 5;

    const AotFunctionDescriptorV5* caller = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/add_then_increment")
            == 0) {
            caller = &module.functions[index];
            break;
        }
    }
    if (caller == nullptr)
        return 6;
    NestedBoundaryState nestedBoundary;
    DirectNativeResumeStateV5 nestedResume;
    context.resetBudget(module.budget);
    context.operationBoundary = &suspendNestedAddOnce;
    context.operationBoundaryUserData = &nestedBoundary;
    error = {};
    caller->entry(context, args, 2u, &nestedResume, &error);
    if (error.code != ScriptDiagnosticCode::Suspended || nestedResume.frame == nullptr
        || nestedResume.pc != 2u)
        return 7;
    error = {};
    const auto nestedResult = caller->entry(context, nullptr, 0u, &nestedResume, &error);
    value = 0;
    if (!(error.code == ScriptDiagnosticCode::None && tryGetInt(nestedResult, value) && value == 6
            && nestedResume.frame == nullptr && nestedBoundary.callerVisits[0] == 1u
            && nestedBoundary.callerVisits[1] == 1u && nestedBoundary.callerVisits[2] == 2u
            && nestedBoundary.addVisits[0] == 1u && nestedBoundary.addVisits[1] == 1u
            && nestedBoundary.addVisits[2] == 2u))
        return 8;

    const AotFunctionDescriptorV5* appendThenLength = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/append_then_length")
            == 0) {
            appendThenLength = &module.functions[index];
            break;
        }
    }
    if (appendThenLength == nullptr)
        return 9;
    SideEffectBoundaryState sideEffectBoundary;
    DirectNativeResumeStateV5 sideEffectResume;
    context.resetBudget(module.budget);
    context.operationBoundary = &suspendAfterAppend;
    context.operationBoundaryUserData = &sideEffectBoundary;
    error = {};
    appendThenLength->entry(context, nullptr, 0u, &sideEffectResume, &error);
    if (error.code != ScriptDiagnosticCode::Suspended || sideEffectResume.frame == nullptr
        || sideEffectResume.pc != 5u || sideEffectResume.commitState != 0u
        || sideEffectBoundary.visits[4] != 1u)
        return 10;
    error = {};
    const auto sideEffectResult =
        appendThenLength->entry(context, nullptr, 0u, &sideEffectResume, &error);
    value = 0;
    if (!(error.code == ScriptDiagnosticCode::None && tryGetInt(sideEffectResult, value)
            && value == 1 && sideEffectResume.frame == nullptr && sideEffectBoundary.visits[4] == 1u
            && sideEffectBoundary.visits[5] == 2u && context.continuationFrameCount == 0u))
        return 11;

    const AotFunctionDescriptorV5* awaitThenYield = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/await_then_yield") == 0) {
            awaitThenYield = &module.functions[index];
            break;
        }
    }
    if (awaitThenYield == nullptr)
        return 20;
    auto awaitContext = arp::core::makeShared<DirectNativeExecutionContext>();
    awaitContext->moduleId = module.moduleId;
    awaitContext->moduleVersion = module.moduleVersion;
    awaitContext->artifactIdentity = module.artifactIdentity;
    awaitContext->moduleDescriptor = &module;
    awaitContext->packageDescriptor = registeredPackage;
    awaitContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    awaitContext->lifetimeOwner = awaitContext;
    awaitContext->resetBudget(module.budget);
    bool canonicalAwaitReady = false;
    std::size_t canonicalAwaitPolls = 0u;
    ScriptAwaitable canonicalGate;
    canonicalGate.poll = [&canonicalAwaitReady, &canonicalAwaitPolls](
                             Value& awaited, arp::core::pmr::String&) {
        ++canonicalAwaitPolls;
        if (!canonicalAwaitReady)
            return ScriptAwaitStatus::Pending;
        awaited = makeInt(4);
        return ScriptAwaitStatus::Ready;
    };
    const Value canonicalAwaitArgs[] = {Value{canonicalGate}};
    error = {};
    Value awaitGeneratorValue =
        awaitThenYield->entry(*awaitContext, canonicalAwaitArgs, 1u, nullptr, &error);
    auto* awaitGenerator = asDirectNativeGeneratorV5(awaitGeneratorValue);
    if (awaitGenerator == nullptr || error.code != ScriptDiagnosticCode::None)
        return 21;
    (void)resumeDirectNativeGeneratorV5(*awaitGenerator, &error);
    if (error.code != ScriptDiagnosticCode::Suspended || canonicalAwaitPolls != 1u
        || awaitGenerator->state == nullptr || awaitGenerator->state->resume.frame == nullptr)
        return 22;
    SuspendedScriptFrame awaitSnapshot;
    if (!transferDirectNativeResumeStateV5(
            *awaitContext, awaitGenerator->state->resume, awaitSnapshot)
            .ok()
        || !awaitSnapshot.awaiting || !awaitSnapshot.suspendedValue.is<ScriptAwaitable>())
        return 23;

    auto adoptedContext = arp::core::makeShared<DirectNativeExecutionContext>();
    adoptedContext->moduleId = module.moduleId;
    adoptedContext->moduleVersion = module.moduleVersion;
    adoptedContext->artifactIdentity = module.artifactIdentity;
    adoptedContext->moduleDescriptor = &module;
    adoptedContext->packageDescriptor = registeredPackage;
    adoptedContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    adoptedContext->lifetimeOwner = adoptedContext;
    adoptedContext->resetBudget(module.budget);
    DirectNativeResumeStateV5 adoptedAwait;
    if (!adoptDirectNativeSuspendedFrameV5(*adoptedContext, module, awaitSnapshot, adoptedAwait)
            .ok())
        return 24;
    canonicalAwaitReady = true;
    error = {};
    const Value adoptedYield =
        awaitThenYield->entry(*adoptedContext, nullptr, 0u, &adoptedAwait, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::Suspended || !tryGetInt(adoptedYield, value)
        || value != 4 || canonicalAwaitPolls != 2u) {
        return 25;
    }

    ScriptAotPackageRegistry registry;
    if (!registry.registerPackage(*registeredPackage).ok())
        return 12;
    ScriptAotModuleInstanceV5 instance;
    if (!createAotOnlyModuleInstanceV5(
            registry, registeredPackage->packageIdentity, "basic", instance)
            .ok())
        return 13;
    if (!configureAotOnlyBoundCallV5(instance, &invokeReflectionFixture, nullptr).ok())
        return 16;
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    error = {};
    const auto dependencyResult =
        dispatcher.invoke("arp-fn:basic/multiply_from_dependency", args, 2u, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(dependencyResult, value)
        || value != 6)
        return 14;
    error = {};
    const auto constantResult =
        dispatcher.invoke("arp-fn:basic/dependency_constant", nullptr, 0u, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(constantResult, value) || value != 3)
        return 15;
    const Value reflectionArgs[] = {makeInt(40), makeInt(2)};
    error = {};
    const auto reflectionResult =
        dispatcher.invoke("arp-fn:basic/reflected_get", reflectionArgs, 2u, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(reflectionResult, value)
        || value != 42)
        return 17;
    const Value closureArgs[] = {makeInt(10)};
    error = {};
    const auto closureResult =
        dispatcher.invoke("arp-fn:basic/closure_accumulate", closureArgs, 1u, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(closureResult, value) || value != 15)
        return 18;
    error = {};
    const auto handledResult = dispatcher.invoke("arp-fn:basic/handled_raise", nullptr, 0u, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(handledResult, value) || value != 7)
        return 19;
    error = {};
    const auto childHandledResult =
        dispatcher.invoke("arp-fn:basic/handled_child_raise", nullptr, 0u, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(childHandledResult, value)
        || value != 8)
        return 20;
    error = {};
    const auto semanticHandledResult =
        dispatcher.invoke("arp-fn:basic/handled_divide_by_zero", nullptr, 0u, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(semanticHandledResult, value)
        || value != 9)
        return 21;

    const Value generatorArgs[] = {makeInt(10)};
    error = {};
    Value generatorValue = dispatcher.invoke("arp-fn:basic/count_two", generatorArgs, 1u, &error);
    auto* generator = asDirectNativeGeneratorV5(generatorValue);
    if (error.code != ScriptDiagnosticCode::None || generator == nullptr)
        return 22;
    auto yielded = resumeDirectNativeGeneratorV5(*generator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 10)
        return 23;
    yielded = sendDirectNativeGeneratorV5(*generator, makeInt(99), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 11)
        return 24;
    const auto generatorReturn = resumeDirectNativeGeneratorV5(*generator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(generatorReturn, value)
        || value != 12 || generator->state == nullptr || !generator->state->done)
        return 25;

    const Value delegatedArgs[] = {makeInt(10)};
    error = {};
    Value delegatedValue =
        dispatcher.invoke("arp-fn:basic/delegated_parent", delegatedArgs, 1u, &error);
    auto* delegated = asDirectNativeGeneratorV5(delegatedValue);
    if (error.code != ScriptDiagnosticCode::None || delegated == nullptr)
        return 33;
    yielded = resumeDirectNativeGeneratorV5(*delegated, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 10)
        return 34;
    // A delegated child owns the resumable backend state while the parent is
    // parked after its Yield. Canonical state carries the source slot and
    // child identity, so a same-backend adoption can continue without replay.
    SuspendedScriptFrame delegatedSnapshot;
    const auto delegatedSnapshotResult = transferDirectNativeResumeStateV5(
        *delegated->state->context, delegated->state->resume, delegatedSnapshot);
    if (!delegatedSnapshotResult.ok() || !delegatedSnapshot.delegatedActive
        || delegatedSnapshot.delegatedRangePc < 0 || delegatedSnapshot.delegatedYieldPc < 0
        || delegated->state == nullptr || delegated->state->done)
        return 58;
    error = {};
    (void)resumeDirectNativeGeneratorV5(*delegated, &error);
    if (error.code != ScriptDiagnosticCode::AotSwitchUnsupported)
        return 58;
    const AotFunctionDescriptorV5* delegatedParent = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/delegated_parent") == 0) {
            delegatedParent = &module.functions[index];
            break;
        }
    }
    if (delegatedParent == nullptr)
        return 59;
    auto adoptedDelegatedContext = arp::core::makeShared<DirectNativeExecutionContext>();
    adoptedDelegatedContext->moduleId = module.moduleId;
    adoptedDelegatedContext->moduleVersion = module.moduleVersion;
    adoptedDelegatedContext->artifactIdentity = module.artifactIdentity;
    adoptedDelegatedContext->moduleDescriptor = &module;
    adoptedDelegatedContext->packageDescriptor = registeredPackage;
    adoptedDelegatedContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    adoptedDelegatedContext->lifetimeOwner = adoptedDelegatedContext;
    adoptedDelegatedContext->resetBudget(module.budget);
    DirectNativeResumeStateV5 adoptedDelegated;
    if (!adoptDirectNativeSuspendedFrameV5(
            *adoptedDelegatedContext, module, delegatedSnapshot, adoptedDelegated)
            .ok())
        return 60;
    adoptedDelegated.injectionKind = DirectNativeInjectionKindV5::Send;
    adoptedDelegated.injectionValue = makeInt(42);
    error = {};
    const Value adoptedDelegatedResult =
        delegatedParent->entry(*adoptedDelegatedContext, nullptr, 0u, &adoptedDelegated, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(adoptedDelegatedResult, value)
        || value != 99 || adoptedDelegated.frame != nullptr)
        return 60;

    // Sequential yield-from sites are separately proven. The second transfer
    // must retain its own RangeNext/source metadata rather than inheriting the
    // first child site from the generated function.
    const AotFunctionDescriptorV5* delegatedPair = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/delegated_pair") == 0) {
            delegatedPair = &module.functions[index];
            break;
        }
    }
    if (delegatedPair == nullptr)
        return 79;
    auto delegatedPairContext = arp::core::makeShared<DirectNativeExecutionContext>();
    delegatedPairContext->moduleId = module.moduleId;
    delegatedPairContext->moduleVersion = module.moduleVersion;
    delegatedPairContext->artifactIdentity = module.artifactIdentity;
    delegatedPairContext->moduleDescriptor = &module;
    delegatedPairContext->packageDescriptor = registeredPackage;
    delegatedPairContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    delegatedPairContext->lifetimeOwner = delegatedPairContext;
    delegatedPairContext->resetBudget(module.budget);
    error = {};
    const Value delegatedPairValue =
        delegatedPair->entry(*delegatedPairContext, delegatedArgs, 1u, nullptr, &error);
    auto* delegatedPairGenerator = asDirectNativeGeneratorV5(delegatedPairValue);
    if (error.code != ScriptDiagnosticCode::None || delegatedPairGenerator == nullptr)
        return 80;
    const Value firstPairYield = resumeDirectNativeGeneratorV5(*delegatedPairGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(firstPairYield, value)
        || value != 10)
        return 81;
    const Value secondPairYield =
        sendDirectNativeGeneratorV5(*delegatedPairGenerator, makeInt(42), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(secondPairYield, value)
        || value != 20 || delegatedPairGenerator->state == nullptr
        || delegatedPairGenerator->state->resume.frame == nullptr)
        return 82;
    SuspendedScriptFrame delegatedPairSnapshot;
    if (!transferDirectNativeResumeStateV5(
            *delegatedPairContext, delegatedPairGenerator->state->resume, delegatedPairSnapshot)
            .ok()
        || !delegatedPairSnapshot.delegatedActive || delegatedPairSnapshot.delegatedRangePc < 0
        || delegatedPairSnapshot.delegatedYieldPc < 0)
        return 83;
    auto adoptedPairContext = arp::core::makeShared<DirectNativeExecutionContext>();
    adoptedPairContext->moduleId = module.moduleId;
    adoptedPairContext->moduleVersion = module.moduleVersion;
    adoptedPairContext->artifactIdentity = module.artifactIdentity;
    adoptedPairContext->moduleDescriptor = &module;
    adoptedPairContext->packageDescriptor = registeredPackage;
    adoptedPairContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    adoptedPairContext->lifetimeOwner = adoptedPairContext;
    adoptedPairContext->resetBudget(module.budget);
    DirectNativeResumeStateV5 adoptedPair;
    if (!adoptDirectNativeSuspendedFrameV5(
            *adoptedPairContext, module, delegatedPairSnapshot, adoptedPair)
            .ok())
        return 84;
    adoptedPair.injectionKind = DirectNativeInjectionKindV5::Send;
    adoptedPair.injectionValue = makeInt(42);
    error = {};
    const Value adoptedPairResult =
        delegatedPair->entry(*adoptedPairContext, nullptr, 0u, &adoptedPair, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(adoptedPairResult, value)
        || value != 199 || adoptedPair.frame != nullptr)
        return 85;

    // The second static delegation site must retain both its canonical
    // identity and injection ownership after transfer. Throwing there must
    // reach the child exactly once and terminally unwind the parent.
    auto throwingPairContext = arp::core::makeShared<DirectNativeExecutionContext>();
    throwingPairContext->moduleId = module.moduleId;
    throwingPairContext->moduleVersion = module.moduleVersion;
    throwingPairContext->artifactIdentity = module.artifactIdentity;
    throwingPairContext->moduleDescriptor = &module;
    throwingPairContext->packageDescriptor = registeredPackage;
    throwingPairContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    throwingPairContext->lifetimeOwner = throwingPairContext;
    throwingPairContext->resetBudget(module.budget);
    error = {};
    const Value throwingPairValue =
        delegatedPair->entry(*throwingPairContext, delegatedArgs, 1u, nullptr, &error);
    auto* throwingPair = asDirectNativeGeneratorV5(throwingPairValue);
    if (error.code != ScriptDiagnosticCode::None || throwingPair == nullptr)
        return 92;
    (void)resumeDirectNativeGeneratorV5(*throwingPair, &error);
    const Value throwingPairSecondYield =
        sendDirectNativeGeneratorV5(*throwingPair, makeInt(42), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(throwingPairSecondYield, value)
        || value != 20 || throwingPair->state == nullptr
        || throwingPair->state->resume.frame == nullptr)
        return 93;
    SuspendedScriptFrame throwingPairSnapshot;
    if (!transferDirectNativeResumeStateV5(
            *throwingPairContext, throwingPair->state->resume, throwingPairSnapshot)
            .ok()
        || !throwingPairSnapshot.delegatedActive)
        return 94;
    auto adoptedThrowingPairContext = arp::core::makeShared<DirectNativeExecutionContext>();
    adoptedThrowingPairContext->moduleId = module.moduleId;
    adoptedThrowingPairContext->moduleVersion = module.moduleVersion;
    adoptedThrowingPairContext->artifactIdentity = module.artifactIdentity;
    adoptedThrowingPairContext->moduleDescriptor = &module;
    adoptedThrowingPairContext->packageDescriptor = registeredPackage;
    adoptedThrowingPairContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    adoptedThrowingPairContext->lifetimeOwner = adoptedThrowingPairContext;
    adoptedThrowingPairContext->resetBudget(module.budget);
    DirectNativeResumeStateV5 adoptedThrowingPair;
    if (!adoptDirectNativeSuspendedFrameV5(
            *adoptedThrowingPairContext, module, throwingPairSnapshot, adoptedThrowingPair)
            .ok())
        return 95;
    adoptedThrowingPair.injectionKind = DirectNativeInjectionKindV5::Throw;
    adoptedThrowingPair.injectionValue = makeString(std::string_view("pair-error"));
    error = {};
    const Value adoptedThrowingPairResult = delegatedPair->entry(
        *adoptedThrowingPairContext, nullptr, 0u, &adoptedThrowingPair, &error);
    if (!isNil(adoptedThrowingPairResult) || error.code != ScriptDiagnosticCode::UserRaise
        || error.message != "pair-error" || adoptedThrowingPair.frame != nullptr)
        return 96;

    // Closing at the second site releases the current child and leaves the
    // parent terminal; a subsequent resume has no observable continuation.
    auto closingPairContext = arp::core::makeShared<DirectNativeExecutionContext>();
    closingPairContext->moduleId = module.moduleId;
    closingPairContext->moduleVersion = module.moduleVersion;
    closingPairContext->artifactIdentity = module.artifactIdentity;
    closingPairContext->moduleDescriptor = &module;
    closingPairContext->packageDescriptor = registeredPackage;
    closingPairContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    closingPairContext->lifetimeOwner = closingPairContext;
    closingPairContext->resetBudget(module.budget);
    error = {};
    const Value closingPairValue =
        delegatedPair->entry(*closingPairContext, delegatedArgs, 1u, nullptr, &error);
    auto* closingPair = asDirectNativeGeneratorV5(closingPairValue);
    if (error.code != ScriptDiagnosticCode::None || closingPair == nullptr)
        return 97;
    (void)resumeDirectNativeGeneratorV5(*closingPair, &error);
    const Value closingPairSecondYield =
        sendDirectNativeGeneratorV5(*closingPair, makeInt(42), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(closingPairSecondYield, value)
        || value != 20)
        return 98;
    closeDirectNativeGeneratorV5(*closingPair);
    if (closingPair->state == nullptr || !closingPair->state->done)
        return 99;
    error = {};
    if (!isNil(resumeDirectNativeGeneratorV5(*closingPair, &error))
        || error.code != ScriptDiagnosticCode::None)
        return 100;

    // A plain generator for-loop has the same bytecode shape as yield-from.
    // Its send() must be consumed by the parent generator, never forwarded to
    // the completed delegated child or rejected as a collection injection.
    const AotFunctionDescriptorV5* delegatedWithPlainFor = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/delegated_with_plain_for")
            == 0) {
            delegatedWithPlainFor = &module.functions[index];
            break;
        }
    }
    if (delegatedWithPlainFor == nullptr)
        return 86;
    auto delegatedWithPlainForContext = arp::core::makeShared<DirectNativeExecutionContext>();
    delegatedWithPlainForContext->moduleId = module.moduleId;
    delegatedWithPlainForContext->moduleVersion = module.moduleVersion;
    delegatedWithPlainForContext->artifactIdentity = module.artifactIdentity;
    delegatedWithPlainForContext->moduleDescriptor = &module;
    delegatedWithPlainForContext->packageDescriptor = registeredPackage;
    delegatedWithPlainForContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    delegatedWithPlainForContext->lifetimeOwner = delegatedWithPlainForContext;
    delegatedWithPlainForContext->resetBudget(module.budget);
    error = {};
    const Value delegatedWithPlainForValue = delegatedWithPlainFor->entry(
        *delegatedWithPlainForContext, delegatedArgs, 1u, nullptr, &error);
    auto* delegatedWithPlainForGenerator = asDirectNativeGeneratorV5(delegatedWithPlainForValue);
    if (error.code != ScriptDiagnosticCode::None || delegatedWithPlainForGenerator == nullptr)
        return 87;
    const Value delegatedWithPlainForFirst =
        resumeDirectNativeGeneratorV5(*delegatedWithPlainForGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(delegatedWithPlainForFirst, value)
        || value != 10)
        return 88;
    const Value plainForFirst =
        sendDirectNativeGeneratorV5(*delegatedWithPlainForGenerator, makeInt(42), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(plainForFirst, value) || value != 30)
        return 89;
    const Value plainForSecond =
        sendDirectNativeGeneratorV5(*delegatedWithPlainForGenerator, makeInt(43), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(plainForSecond, value)
        || value != 31)
        return 90;
    const Value plainForResult =
        resumeDirectNativeGeneratorV5(*delegatedWithPlainForGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(plainForResult, value)
        || value != 200)
        return 91;

    SuspendedScriptFrame incompleteDelegated;
    incompleteDelegated.moduleId = module.moduleId;
    incompleteDelegated.moduleVersion = module.moduleVersion;
    incompleteDelegated.artifactIdentity = module.artifactIdentity;
    incompleteDelegated.functionId = delegatedParent->functionId;
    incompleteDelegated.safepointId = "delegated_parent:pc:3";
    incompleteDelegated.bytecodePc = 3;
    incompleteDelegated.source = SourcePos{
        arp::core::pmr::String("aot:delegated_parent", &arp::core::defaultCpuMemoryResource()),
        96,
        5};
    incompleteDelegated.locals.resize(4u, makeNil());
    incompleteDelegated.delegatedActive = true;
    incompleteDelegated.delegatedChildAwaiting = true;
    incompleteDelegated.delegatedSourceSlot = 0;
    incompleteDelegated.delegatedYieldPc = 3;
    incompleteDelegated.delegatedPendingYieldPc = 3;
    DirectNativeResumeStateV5 rejectedDelegated;
    const auto rejectedDelegatedResult = adoptDirectNativeSuspendedFrameV5(
        *delegated->state->context, module, incompleteDelegated, rejectedDelegated);
    if (rejectedDelegatedResult.ok()
        || rejectedDelegatedResult.diagnostic().code != ScriptDiagnosticCode::TypedIrInvalid
        || delegated->state == nullptr || delegated->state->done)
        return 61;

    bool delegatedAwaitReady = false;
    std::size_t delegatedAwaitPolls = 0u;
    ScriptAwaitable delegatedAwaitGate;
    delegatedAwaitGate.poll = [&delegatedAwaitReady, &delegatedAwaitPolls](
                                  Value& awaited, arp::core::pmr::String&) {
        ++delegatedAwaitPolls;
        if (!delegatedAwaitReady)
            return ScriptAwaitStatus::Pending;
        awaited = makeInt(5);
        return ScriptAwaitStatus::Ready;
    };
    const Value delegatedAwaitArgs[] = {Value{delegatedAwaitGate}};
    error = {};
    Value delegatedAwaitParentValue =
        dispatcher.invoke("arp-fn:basic/delegated_await_parent", delegatedAwaitArgs, 1u, &error);
    auto* delegatedAwaitParent = asDirectNativeGeneratorV5(delegatedAwaitParentValue);
    if (error.code != ScriptDiagnosticCode::None || delegatedAwaitParent == nullptr)
        return 72;
    (void)resumeDirectNativeGeneratorV5(*delegatedAwaitParent, &error);
    if (error.code != ScriptDiagnosticCode::Suspended || delegatedAwaitPolls != 1u
        || delegatedAwaitParent->state == nullptr
        || delegatedAwaitParent->state->resume.frame == nullptr)
        return 73;
    SuspendedScriptFrame delegatedAwaitSnapshot;
    const auto delegatedAwaitSnapshotResult =
        transferDirectNativeResumeStateV5(*delegatedAwaitParent->state->context,
            delegatedAwaitParent->state->resume,
            delegatedAwaitSnapshot);
    if (!delegatedAwaitSnapshotResult.ok() || !delegatedAwaitSnapshot.delegatedChildAwaiting
        || delegatedAwaitSnapshot.delegatedRangePc < 0
        || delegatedAwaitSnapshot.delegatedPendingYieldPc < 0)
        return 74;
    auto delegatedAwaitContext = arp::core::makeShared<DirectNativeExecutionContext>();
    delegatedAwaitContext->moduleId = module.moduleId;
    delegatedAwaitContext->moduleVersion = module.moduleVersion;
    delegatedAwaitContext->artifactIdentity = module.artifactIdentity;
    delegatedAwaitContext->moduleDescriptor = &module;
    delegatedAwaitContext->packageDescriptor = registeredPackage;
    delegatedAwaitContext->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    delegatedAwaitContext->lifetimeOwner = delegatedAwaitContext;
    delegatedAwaitContext->resetBudget(module.budget);
    DirectNativeResumeStateV5 delegatedAwaitResume;
    if (!adoptDirectNativeSuspendedFrameV5(
            *delegatedAwaitContext, module, delegatedAwaitSnapshot, delegatedAwaitResume)
            .ok())
        return 75;
    delegatedAwaitReady = true;
    error = {};
    const AotFunctionDescriptorV5* delegatedAwaitParentEntry = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/delegated_await_parent")
            == 0) {
            delegatedAwaitParentEntry = &module.functions[index];
            break;
        }
    }
    if (delegatedAwaitParentEntry == nullptr)
        return 76;
    const Value delegatedAwaitYield = delegatedAwaitParentEntry->entry(
        *delegatedAwaitContext, nullptr, 0u, &delegatedAwaitResume, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::Suspended || !tryGetInt(delegatedAwaitYield, value)
        || value != 5 || delegatedAwaitPolls != 2u)
        return 77;
    delegatedAwaitResume.injectionKind = DirectNativeInjectionKindV5::Send;
    delegatedAwaitResume.injectionValue = makeNil();
    error = {};
    const Value delegatedAwaitResult = delegatedAwaitParentEntry->entry(
        *delegatedAwaitContext, nullptr, 0u, &delegatedAwaitResume, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(delegatedAwaitResult, value)
        || value != 9 || delegatedAwaitResume.frame != nullptr)
        return 78;

    // Dynamic `Any` delegation is admitted only through the explicit runtime
    // tri-state contract. A DirectNative child is forwarded with its identity
    // checks and terminal result; no static function provenance is guessed.
    error = {};
    const Value dynamicChildArgs[] = {makeInt(12)};
    Value dynamicChildValue =
        dispatcher.invoke("arp-fn:basic/delegated_child", dynamicChildArgs, 1u, &error);
    auto* dynamicChild = asDirectNativeGeneratorV5(dynamicChildValue);
    if (error.code != ScriptDiagnosticCode::None || dynamicChild == nullptr)
        return 62;
    const Value dynamicParentArgs[] = {dynamicChildValue};
    Value dynamicParentValue =
        dispatcher.invoke("arp-fn:basic/delegated_dynamic_parent", dynamicParentArgs, 1u, &error);
    auto* dynamicParent = asDirectNativeGeneratorV5(dynamicParentValue);
    if (error.code != ScriptDiagnosticCode::None || dynamicParent == nullptr)
        return 63;
    yielded = resumeDirectNativeGeneratorV5(*dynamicParent, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 12)
        return 64;
    int dynamicReturn = 0;
    const Value dynamicCompleted = sendDirectNativeGeneratorV5(*dynamicParent, makeInt(7), &error);
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(dynamicCompleted, dynamicReturn)
        || dynamicReturn != 88 || dynamicParent->state == nullptr || !dynamicParent->state->done)
        return 65;

    // A dynamic collection follows the same explicit collection lowering.
    error = {};
    arp::reflection::hlist dynamicCollectionList{&arp::core::defaultCpuMemoryResource()};
    dynamicCollectionList.push_back(makeInt(3));
    dynamicCollectionList.push_back(makeInt(5));
    Value dynamicCollection{std::move(dynamicCollectionList)};
    const Value dynamicCollectionArgs[] = {dynamicCollection};
    Value dynamicCollectionValue = dispatcher.invoke(
        "arp-fn:basic/delegated_dynamic_parent", dynamicCollectionArgs, 1u, &error);
    auto* dynamicCollectionGenerator = asDirectNativeGeneratorV5(dynamicCollectionValue);
    if (error.code != ScriptDiagnosticCode::None || dynamicCollectionGenerator == nullptr)
        return 66;
    yielded = resumeDirectNativeGeneratorV5(*dynamicCollectionGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 3)
        return 67;
    yielded = resumeDirectNativeGeneratorV5(*dynamicCollectionGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 5)
        return 68;
    yielded = resumeDirectNativeGeneratorV5(*dynamicCollectionGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 88
        || dynamicCollectionGenerator->state == nullptr || !dynamicCollectionGenerator->state->done)
        return 69;

    // Unsupported dynamic values must fail at the semantic range boundary,
    // never become an empty generator or a silent compatibility path.
    error = {};
    const Value invalidDynamicArgs[] = {makeInt(1)};
    Value invalidDynamicValue =
        dispatcher.invoke("arp-fn:basic/delegated_dynamic_parent", invalidDynamicArgs, 1u, &error);
    auto* invalidDynamicGenerator = asDirectNativeGeneratorV5(invalidDynamicValue);
    if (error.code != ScriptDiagnosticCode::None || invalidDynamicGenerator == nullptr)
        return 70;
    (void)resumeDirectNativeGeneratorV5(*invalidDynamicGenerator, &error);
    if (error.code == ScriptDiagnosticCode::None || !invalidDynamicGenerator->state->done)
        return 71;

    error = {};
    Value importedDelegatedValue =
        dispatcher.invoke("arp-fn:basic/delegated_import_parent", delegatedArgs, 1u, &error);
    auto* importedDelegated = asDirectNativeGeneratorV5(importedDelegatedValue);
    if (error.code != ScriptDiagnosticCode::None || importedDelegated == nullptr)
        return 49;
    yielded = resumeDirectNativeGeneratorV5(*importedDelegated, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 10)
        return 50;
    const auto importedDelegatedReturn =
        sendDirectNativeGeneratorV5(*importedDelegated, makeInt(42), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(importedDelegatedReturn, value)
        || value != 99 || importedDelegated->state == nullptr || !importedDelegated->state->done)
        return 51;

    error = {};
    Value throwingDelegatedValue =
        dispatcher.invoke("arp-fn:basic/delegated_parent", delegatedArgs, 1u, &error);
    auto* throwingDelegated = asDirectNativeGeneratorV5(throwingDelegatedValue);
    if (error.code != ScriptDiagnosticCode::None || throwingDelegated == nullptr)
        return 37;
    yielded = resumeDirectNativeGeneratorV5(*throwingDelegated, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 10)
        return 38;
    const Value delegatedThrown = throwDirectNativeGeneratorV5(
        *throwingDelegated, makeString(std::string_view("delegated-error")), &error);
    if (!isNil(delegatedThrown) || error.code != ScriptDiagnosticCode::UserRaise
        || error.message != "delegated-error" || throwingDelegated->state == nullptr
        || !throwingDelegated->state->done)
        return 39;

    error = {};
    Value closingDelegatedValue =
        dispatcher.invoke("arp-fn:basic/delegated_parent", delegatedArgs, 1u, &error);
    auto* closingDelegated = asDirectNativeGeneratorV5(closingDelegatedValue);
    if (error.code != ScriptDiagnosticCode::None || closingDelegated == nullptr)
        return 40;
    yielded = resumeDirectNativeGeneratorV5(*closingDelegated, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 10)
        return 41;
    closeDirectNativeGeneratorV5(*closingDelegated);
    if (closingDelegated->state == nullptr || !closingDelegated->state->done)
        return 42;
    error = {};
    if (!isNil(resumeDirectNativeGeneratorV5(*closingDelegated, &error))
        || error.code != ScriptDiagnosticCode::None)
        return 43;

    error = {};
    const Value recursiveArgs[] = {makeInt(2)};
    Value recursiveValue =
        dispatcher.invoke("arp-fn:basic/recursive_delegated", recursiveArgs, 1u, &error);
    auto* recursive = asDirectNativeGeneratorV5(recursiveValue);
    if (error.code != ScriptDiagnosticCode::None || recursive == nullptr)
        return 52;
    yielded = resumeDirectNativeGeneratorV5(*recursive, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 1)
        return 53;
    yielded = resumeDirectNativeGeneratorV5(*recursive, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 2
        || recursive->state == nullptr || !recursive->state->done
        || recursive->state->resume.frame != nullptr)
        return 54;
    yielded = resumeDirectNativeGeneratorV5(*recursive, &error);
    if (!isNil(yielded) || error.code != ScriptDiagnosticCode::None || recursive->state == nullptr
        || !recursive->state->done)
        return 55;

    error = {};
    const Value overflowingRecursiveArgs[] = {makeInt(100)};
    Value overflowingRecursiveValue =
        dispatcher.invoke("arp-fn:basic/recursive_delegated", overflowingRecursiveArgs, 1u, &error);
    auto* overflowingRecursive = asDirectNativeGeneratorV5(overflowingRecursiveValue);
    if (error.code != ScriptDiagnosticCode::None || overflowingRecursive == nullptr)
        return 56;
    yielded = resumeDirectNativeGeneratorV5(*overflowingRecursive, &error);
    if (!isNil(yielded) || error.code != ScriptDiagnosticCode::RecursionLimitExceeded
        || overflowingRecursive->state == nullptr || !overflowingRecursive->state->done
        || overflowingRecursive->state->resume.frame != nullptr)
        return 57;

    error = {};
    Value collectionValue =
        dispatcher.invoke("arp-fn:basic/delegated_collection_parent", nullptr, 0u, &error);
    auto* collectionGenerator = asDirectNativeGeneratorV5(collectionValue);
    if (error.code != ScriptDiagnosticCode::None || collectionGenerator == nullptr)
        return 44;
    yielded = resumeDirectNativeGeneratorV5(*collectionGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 4)
        return 45;
    yielded = sendDirectNativeGeneratorV5(*collectionGenerator, makeNil(), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 7)
        return 46;
    yielded = sendDirectNativeGeneratorV5(*collectionGenerator, makeInt(2), &error);
    if (!isNil(yielded) || error.code != ScriptDiagnosticCode::AotSwitchUnsupported
        || collectionGenerator->state == nullptr || collectionGenerator->state->done)
        return 47;
    yielded = throwDirectNativeGeneratorV5(
        *collectionGenerator, makeString(std::string_view("collection-error")), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 8
        || collectionGenerator->state == nullptr || !collectionGenerator->state->done)
        return 48;

    error = {};
    Value throwingGeneratorValue =
        dispatcher.invoke("arp-fn:basic/handled_generator_throw", nullptr, 0u, &error);
    auto* throwingGenerator = asDirectNativeGeneratorV5(throwingGeneratorValue);
    if (error.code != ScriptDiagnosticCode::None || throwingGenerator == nullptr)
        return 26;
    yielded = resumeDirectNativeGeneratorV5(*throwingGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 1)
        return 27;
    yielded = throwDirectNativeGeneratorV5(
        *throwingGenerator, makeString(std::string_view("injected")), &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || !tryGetInt(yielded, value) || value != 2)
        return 28;

    int polls = 0;
    ScriptAwaitable gate;
    gate.poll = [&polls](Value& result, arp::core::pmr::String&) {
        ++polls;
        if (polls == 1)
            return ScriptAwaitStatus::Pending;
        result = makeInt(42);
        return ScriptAwaitStatus::Ready;
    };
    const Value awaitArgs[] = {Value{std::move(gate)}};
    error = {};
    Value awaitingGeneratorValue =
        dispatcher.invoke("arp-fn:basic/await_then_yield", awaitArgs, 1u, &error);
    auto* awaitingGenerator = asDirectNativeGeneratorV5(awaitingGeneratorValue);
    if (error.code != ScriptDiagnosticCode::None || awaitingGenerator == nullptr)
        return 29;
    resumeDirectNativeGeneratorV5(*awaitingGenerator, &error);
    if (error.code != ScriptDiagnosticCode::Suspended || polls != 1)
        return 30;
    yielded = resumeDirectNativeGeneratorV5(*awaitingGenerator, &error);
    value = 0;
    if (error.code != ScriptDiagnosticCode::None || polls != 2 || !tryGetInt(yielded, value)
        || value != 42)
        return 31;
    closeDirectNativeGeneratorV5(*awaitingGenerator);
    if (awaitingGenerator->state == nullptr || !awaitingGenerator->state->done)
        return 32;

    // A generator may outlive its dispatcher. Its context must retain the
    // package owner (for generated code) while the callback bridge must fail
    // closed once the package runtime owner is gone.
    ScriptAotModuleInstanceV5 ownerInstance;
    if (!createAotOnlyModuleInstanceV5(
            registry, registeredPackage->packageIdentity, "basic", ownerInstance)
            .ok()
        || !configureAotOnlyBoundCallV5(ownerInstance, &invokeReflectionFixture, nullptr).ok())
        return 49;
    Value ownerGeneratorValue;
    {
        DirectNativeDispatcherV5 ownerDispatcher(std::move(ownerInstance));
        const Value ownerArgs[] = {makeInt(40), makeInt(2)};
        error = {};
        ownerGeneratorValue =
            ownerDispatcher.invoke("arp-fn:basic/yielded_reflected", ownerArgs, 2u, &error);
        auto* ownerGenerator = asDirectNativeGeneratorV5(ownerGeneratorValue);
        if (ownerGenerator == nullptr || error.code != ScriptDiagnosticCode::None)
            return 50;
        const auto ownerYield = resumeDirectNativeGeneratorV5(*ownerGenerator, &error);
        value = 0;
        if (!tryGetInt(ownerYield, value) || value != 1 || error.code != ScriptDiagnosticCode::None)
            return 51;
    }
    auto* ownerGenerator = asDirectNativeGeneratorV5(ownerGeneratorValue);
    if (ownerGenerator == nullptr)
        return 52;
    const auto unavailable = resumeDirectNativeGeneratorV5(*ownerGenerator, &error);
    if (!isNil(unavailable) || error.code != ScriptDiagnosticCode::AotModuleUnavailable
        || !ownerGenerator->state->done)
        return 53;

    // The generated frame gate accounts the cumulative frame count in
    // addition to bytes. Two suspended generators sharing one module context
    // must therefore fail closed on the second frame without disturbing the
    // first continuation.
    auto constrainedContext = arp::core::makeShared<DirectNativeExecutionContext>();
    constrainedContext->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    constrainedContext->lifetimeOwner = constrainedContext;
    constrainedContext->resetBudget(module.budget);
    constrainedContext->budget.maxContinuationFrames = 1u;
    VmError constrainedInitError;
    if (!constrainedContext->ensureGlobals(module.globalCount, &constrainedInitError))
        return 54;
    const AotFunctionDescriptorV5* countTwo = nullptr;
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        if (std::strcmp(module.functions[index].functionId, "arp-fn:basic/count_two") == 0) {
            countTwo = &module.functions[index];
            break;
        }
    }
    if (countTwo == nullptr)
        return 55;
    const Value constrainedArgs[] = {makeInt(5)};
    error = {};
    const Value firstConstrainedValue =
        countTwo->entry(*constrainedContext, constrainedArgs, 1u, nullptr, &error);
    auto* firstConstrained = asDirectNativeGeneratorV5(firstConstrainedValue);
    const Value secondConstrainedValue =
        countTwo->entry(*constrainedContext, constrainedArgs, 1u, nullptr, &error);
    auto* secondConstrained = asDirectNativeGeneratorV5(secondConstrainedValue);
    if (firstConstrained == nullptr || secondConstrained == nullptr
        || error.code != ScriptDiagnosticCode::None)
        return 56;
    yielded = resumeDirectNativeGeneratorV5(*firstConstrained, &error);
    value = 0;
    if (!tryGetInt(yielded, value) || value != 5 || error.code != ScriptDiagnosticCode::None
        || firstConstrained->state == nullptr || firstConstrained->state->resume.frame == nullptr)
        return 57;
    const Value rejectedConstrained = resumeDirectNativeGeneratorV5(*secondConstrained, &error);
    if (!isNil(rejectedConstrained) || error.code != ScriptDiagnosticCode::ResourceLimitExceeded
        || secondConstrained->state == nullptr || !secondConstrained->state->done
        || secondConstrained->state->resume.frame != nullptr
        || firstConstrained->state->resume.frame == nullptr)
        return 58;

    return 0;
}

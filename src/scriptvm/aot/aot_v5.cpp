#include "scriptvm/aot/aot_v5.h"

#include <cassert>
#include <cstring>
#include <limits>
#include <new>

#if defined(__GNUC__) || defined(__clang__)
namespace arp::scriptvm {
extern void installGeneratorBridgeV2(const GeneratorBridgeV2*) noexcept __attribute__((weak));
extern const GeneratorBridgeV2* generatorBridgeV2() noexcept __attribute__((weak));
extern void installGeneratorBridgeV3(const GeneratorBridgeV3*) noexcept __attribute__((weak));
extern const GeneratorBridgeV3* generatorBridgeV3() noexcept __attribute__((weak));
extern void installGeneratorBridgeV3Capability(const GeneratorBridgeV3Capability*) noexcept
    __attribute__((weak));
extern const GeneratorBridgeV3Capability* generatorBridgeV3Capability() noexcept
    __attribute__((weak));
} // namespace arp::scriptvm
#endif

namespace arp::scriptvm {
namespace {

class MemoryRootOwnerScope final {
public:
    explicit MemoryRootOwnerScope(DirectNativeExecutionContext& context) noexcept
        : context_(context)
    {}

    ~MemoryRootOwnerScope() { context_.releaseMemoryRoots(this); }

    bool synchronize(const ScriptValueSpan* spans,
        std::size_t spanCount,
        const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
        VmError* error)
    {
        return context_.synchronizeMemoryRootsForOwner(this, spans, spanCount, captures, error);
    }

private:
    DirectNativeExecutionContext& context_;
};

bool directNativeBridgeIsGenerator(const Value& value) noexcept
{
    return isDirectNativeGeneratorV5(value);
}

int directNativeDepthOverflow(int remaining) noexcept
{
    if (remaining <= 0)
        return 1;
    if (remaining == std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return remaining + 1;
}

bool directNativeBridgeQueryIdentity(const Value& value, GeneratorIdentityV3& identity) noexcept
{
    const auto* generator = asDirectNativeGeneratorV5(value);
    if (generator == nullptr || generator->state == nullptr || generator->state->context == nullptr)
        return false;
    const auto& context = *generator->state->context;
    identity.moduleId = context.moduleId.c_str();
    identity.moduleVersion = context.moduleVersion;
    identity.artifactIdentity = context.artifactIdentity.c_str();
    return identity.complete();
}

int directNativeBridgeDelegatedAwaitDepth(const Value& value, int remaining) noexcept
{
    if (remaining <= 0)
        return directNativeDepthOverflow(remaining);
    const auto* generator = asDirectNativeGeneratorV5(value);
    if (generator == nullptr || generator->state == nullptr)
        return 0;
    return generator->state->resume.suspendKind == DirectNativeSuspendKindV5::Await ? 1 : 0;
}

bool directNativeBridgeIsDone(const Value& value) noexcept
{
    const auto* generator = asDirectNativeGeneratorV5(value);
    return generator == nullptr || generator->state == nullptr || generator->state->done;
}

bool directNativeBridgeIsSuspended(const Value& value) noexcept
{
    const auto* generator = asDirectNativeGeneratorV5(value);
    return generator != nullptr && generator->state != nullptr
        && (generator->state->resume.suspendKind != DirectNativeSuspendKindV5::None
            || generator->state->resume.frame != nullptr);
}

bool directNativeBridgeSupportsPendingAwaitInjection(const Value& value) noexcept
{
    const auto* generator = asDirectNativeGeneratorV5(value);
    return generator != nullptr && generator->state != nullptr
        && generator->state->resume.suspendKind == DirectNativeSuspendKindV5::Await
        && generator->state->resume.awaitInjectionSupported;
}

Value directNativeBridgeResume(Value& value, VmError* error)
{
    auto* generator = asDirectNativeGeneratorV5(value);
    return generator == nullptr ? makeNil() : resumeDirectNativeGeneratorV5(*generator, error);
}

Value directNativeBridgeResumeSend(Value& value, const Value& sent, VmError* error)
{
    auto* generator = asDirectNativeGeneratorV5(value);
    return generator == nullptr ? makeNil() : sendDirectNativeGeneratorV5(*generator, sent, error);
}

Value directNativeBridgeResumeThrow(Value& value, const Value& thrown, VmError* error)
{
    auto* generator = asDirectNativeGeneratorV5(value);
    return generator == nullptr ? makeNil()
                                : throwDirectNativeGeneratorV5(*generator, thrown, error);
}

void directNativeBridgeClose(Value& value) noexcept
{
    auto* generator = asDirectNativeGeneratorV5(value);
    if (generator != nullptr)
        closeDirectNativeGeneratorV5(*generator);
}

void installDirectNativeGeneratorBridge() noexcept
{
    // Register the known carrier once when publishing its predicate, rather
    // than through repeated negative owned-collection queries at execution.
    (void)arp::reflection::type_of<DirectNativeGeneratorV5>();
    static const GeneratorBridgeV2 bridge{
        &directNativeBridgeIsGenerator,
        &directNativeBridgeDelegatedAwaitDepth,
        &directNativeBridgeIsDone,
        &directNativeBridgeIsSuspended,
        &directNativeBridgeResume,
        &directNativeBridgeResumeSend,
        &directNativeBridgeResumeThrow,
        &directNativeBridgeClose,
        nullptr,
    };
#if defined(__GNUC__) || defined(__clang__)
    if (installGeneratorBridgeV2 != nullptr)
        installGeneratorBridgeV2(&bridge);
#endif
    static const GeneratorBridgeV3 bridgeV3{
        kGeneratorBridgeV3ContractVersion,
        sizeof(GeneratorBridgeV3),
        &directNativeBridgeIsGenerator,
        &directNativeBridgeDelegatedAwaitDepth,
        &directNativeBridgeIsDone,
        &directNativeBridgeIsSuspended,
        &directNativeBridgeResume,
        &directNativeBridgeResumeSend,
        &directNativeBridgeResumeThrow,
        &directNativeBridgeClose,
        nullptr,
        &directNativeBridgeQueryIdentity,
    };
#if defined(__GNUC__) || defined(__clang__)
    if (installGeneratorBridgeV3 != nullptr)
        installGeneratorBridgeV3(&bridgeV3);
    static const GeneratorBridgeV3Capability capability{
        kGeneratorBridgeV3CapabilityContractVersion,
        sizeof(GeneratorBridgeV3Capability),
        &directNativeBridgeIsGenerator,
        &directNativeBridgeSupportsPendingAwaitInjection,
    };
    if (installGeneratorBridgeV3Capability != nullptr)
        installGeneratorBridgeV3Capability(&capability);
#endif
}

core::pmr::String text(const char* value)
{
    return core::pmr::String(value, &core::defaultCpuMemoryResource());
}

ScriptResult invalid(const char* message)
{
    return ScriptResult::failure(ScriptDiagnosticCode::AotArtifactInvalid, text(message));
}

ScriptResult versionMismatch(const char* message)
{
    return ScriptResult::failure(ScriptDiagnosticCode::AotVersionMismatch, text(message));
}

void memoryLimitExceeded(VmError* error) noexcept
{
    if (error == nullptr)
        return;
    error->code = ScriptDiagnosticCode::ResourceLimitExceeded;
    try {
        error->message = "DirectNative memory budget exceeded";
    } catch (const std::bad_alloc&) {
        error->message.clear();
    }
}

void memoryLimitExceeded(DirectNativeExecutionContext& context, VmError* error) noexcept
{
    context.memoryBudgetFailed = true;
    context.moduleInitialized = false;
    context.globals.clear();
    if (context.memoryAccountant != nullptr)
        context.memoryAccountant->reset();
    memoryLimitExceeded(error);
}

bool memoryLimitReached(const DirectNativeExecutionContext& context) noexcept
{
    return context.exceedsMemoryLimit();
}

bool present(const char* value) noexcept
{
    return value != nullptr && value[0] != '\0';
}

bool validSha256(const char* value) noexcept
{
    if (!present(value) || std::strlen(value) != 71u || std::strncmp(value, "sha256:", 7u) != 0) {
        return false;
    }
    for (std::size_t index = 7u; index < 71u; ++index) {
        const char c = value[index];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool explicitBudget(const AotExecutionBudgetV5& budget) noexcept
{
    return budget.instructionLimit > 0u && budget.recursionLimit > 0u && budget.maxFrameValues > 0u
        && budget.maxContainerValues > 0u && budget.maxCallArguments > 0u
        && budget.maxContinuationFrames > 0u && budget.maxMemoryBytes > 0u;
}

bool validOsrCapability(AotOsrCapability capability) noexcept
{
    constexpr std::uint32_t allowed = static_cast<std::uint32_t>(AotOsrCapability::FunctionBoundary)
        | static_cast<std::uint32_t>(AotOsrCapability::OperationBoundary)
        | static_cast<std::uint32_t>(AotOsrCapability::Resumable);
    const auto value = static_cast<std::uint32_t>(capability);
    return value != 0u && (value & ~allowed) == 0u
        && (value & static_cast<std::uint32_t>(AotOsrCapability::FunctionBoundary)) != 0u;
}

bool validPlatformMask(AotPlatform platforms) noexcept
{
    constexpr std::uint32_t allowed = static_cast<std::uint32_t>(AotPlatform::Windows)
        | static_cast<std::uint32_t>(AotPlatform::WebWasm)
        | static_cast<std::uint32_t>(AotPlatform::MacOS);
    const auto value = static_cast<std::uint32_t>(platforms);
    return value != 0u && (value & ~allowed) == 0u;
}

ScriptResult validateFunction(const AotFunctionDescriptorV5& function)
{
    if (!present(function.functionId) || !present(function.functionIdentity)
        || !validSha256(function.signatureHash) || !present(function.entrySymbol)
        || function.entry == nullptr) {
        return invalid("AOT v5 DirectNative function identity or entry is missing");
    }
    if (function.entryAbiVersion != kDirectNativeEntryAbiVersion) {
        return versionMismatch("AOT v5 function entry ABI is unsupported");
    }
    if (function.executionKind != AotFunctionExecutionKind::DirectNative) {
        return invalid("AOT v5 function is not DirectNative");
    }
    if (!validOsrCapability(function.osrCapability)) {
        return invalid("AOT v5 function OSR capability is invalid");
    }
    return ScriptResult::success();
}

ScriptResult validateManifestModule(const AotPackageManifestModuleV5& module,
    const core::pmr::String& packageIdentity,
    AotPlatform requiredPlatform)
{
    AotModuleDescriptorV5 descriptor;
    descriptor.packageIdentity = packageIdentity.c_str();
    descriptor.moduleId = module.moduleId.c_str();
    descriptor.moduleVersion = module.moduleVersion;
    descriptor.artifactIdentity = module.artifactIdentity.c_str();
    descriptor.exportContractHash = module.exportContractHash.c_str();
    descriptor.sourceHash = module.sourceHash.c_str();
    descriptor.dependencyGraphHash = module.dependencyGraphHash.c_str();
    descriptor.reflectionRegistryHash = module.reflectionRegistryHash.c_str();
    descriptor.hostContractHash = module.hostContractHash.c_str();
    descriptor.semanticAbiVersion = module.semanticAbiVersion;
    descriptor.entryAbiVersion = module.entryAbiVersion;
    descriptor.platforms = module.platforms;
    descriptor.budget = module.budget;
    descriptor.globalCount = module.globalCount;
    descriptor.moduleInitFunctionId =
        module.moduleInitFunctionId.empty() ? nullptr : module.moduleInitFunctionId.c_str();
    if (!present(descriptor.moduleId) || descriptor.moduleVersion == 0u
        || !present(descriptor.artifactIdentity) || !validSha256(descriptor.exportContractHash)
        || !validSha256(descriptor.sourceHash) || !validSha256(descriptor.dependencyGraphHash)
        || !validSha256(descriptor.reflectionRegistryHash)
        || !validSha256(descriptor.hostContractHash) || descriptor.semanticAbiVersion == 0u
        || descriptor.entryAbiVersion != kDirectNativeEntryAbiVersion
        || !hasAotPlatform(descriptor.platforms, requiredPlatform)
        || !explicitBudget(descriptor.budget) || module.functions.empty()
        || (module.globalCount != 0u && module.moduleInitFunctionId.empty())) {
        return invalid("AOT v5 manifest module contract is incomplete");
    }
    for (std::size_t index = 0; index < module.pluginDependencies.size(); ++index) {
        const auto& dependency = module.pluginDependencies[index];
        if (dependency.moduleId.empty() || dependency.moduleVersion == 0u
            || !validSha256(dependency.exportContractHash.c_str())
            || !validSha256(dependency.reflectionRegistryHash.c_str())) {
            return invalid("AOT v5 manifest Plugin dependency contract is incomplete");
        }
        for (std::size_t serviceIndex = 0; serviceIndex < dependency.services.size();
            ++serviceIndex) {
            const auto& service = dependency.services[serviceIndex];
            if (service.serviceId.empty() || service.version == 0u) {
                return invalid("AOT v5 manifest Plugin service dependency is incomplete");
            }
            for (std::size_t other = 0; other < serviceIndex; ++other) {
                if (dependency.services[other].serviceId == service.serviceId) {
                    return invalid("AOT v5 manifest Plugin service dependency is duplicated");
                }
            }
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (module.pluginDependencies[other].moduleId == dependency.moduleId) {
                return invalid("AOT v5 manifest Plugin dependency is duplicated");
            }
        }
    }
    for (std::size_t index = 0; index < module.functions.size(); ++index) {
        const auto& function = module.functions[index];
        if (function.functionId.empty() || function.functionIdentity.empty()
            || !validSha256(function.signatureHash.c_str()) || function.entrySymbol.empty()
            || function.entryAbiVersion != kDirectNativeEntryAbiVersion
            || function.executionKind != AotFunctionExecutionKind::DirectNative
            || !validOsrCapability(function.osrCapability)) {
            return invalid("AOT v5 manifest function contract is incomplete");
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (module.functions[other].functionId == function.functionId
                || module.functions[other].functionIdentity == function.functionIdentity) {
                return invalid("AOT v5 manifest contains duplicate function identity");
            }
        }
    }
    return ScriptResult::success();
}

} // namespace

Value makeDirectNativeGeneratorV5(DirectNativeExecutionContext& context,
    DirectNativeEntryV5 entry,
    const Value* arguments,
    std::size_t argumentCount,
    VmError* error)
{
    installDirectNativeGeneratorBridge();
    auto owner = context.lifetimeOwner.lock();
    if (memoryLimitReached(context)) {
        memoryLimitExceeded(context, error);
        return makeNil();
    }
    if (entry == nullptr || owner == nullptr || (argumentCount != 0u && arguments == nullptr)) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotArtifactInvalid;
            error->message = "DirectNative generator lifetime or entry is unavailable";
        }
        return makeNil();
    }
    try {
        core::CpuMemoryQuotaScope quotaScope{context.memoryQuota};
        auto state = core::makeShared<DirectNativeGeneratorStateV5>();
        state->entry = entry;
        state->context = std::move(owner);
        state->initialArguments.reserve(argumentCount);
        for (std::size_t index = 0; index < argumentCount; ++index)
            state->initialArguments.push_back(arguments[index]);
        if (context.activeCaptures != nullptr)
            state->captures = *context.activeCaptures;
        const ScriptValueSpan argumentRoots{
            state->initialArguments.data(), state->initialArguments.size()};
        if (!context.synchronizeMemoryRootsForOwner(
                state.get(), &argumentRoots, 1u, &state->captures, error))
            return makeNil();
        return Value{DirectNativeGeneratorV5{std::move(state)}};
    } catch (const std::bad_alloc&) {
        memoryLimitExceeded(context, error);
        return makeNil();
    }
}

bool isDirectNativeGeneratorV5(const Value& value)
{
    if (exactValuePayload<DirectNativeGeneratorV5>(value) != nullptr)
        return true;
    if (hasExactOwnedCollectionPayload(value)) {
        assert(!value.is<DirectNativeGeneratorV5>());
        return false;
    }
    return value.is<DirectNativeGeneratorV5>();
}

DirectNativeGeneratorV5* asDirectNativeGeneratorV5(const Value& value)
{
    return isDirectNativeGeneratorV5(value) ? value.ptr<DirectNativeGeneratorV5>() : nullptr;
}

namespace {

void releaseGeneratorMemoryRoots(
    const std::shared_ptr<DirectNativeGeneratorStateV5>& state) noexcept
{
    if (state != nullptr && state->context != nullptr)
        state->context->releaseMemoryRoots(state.get());
}

Value resumeGenerator(DirectNativeGeneratorV5& generator, VmError* error)
{
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::None;
        error->message.clear();
        error->traceback.clear();
    }
    auto state = generator.state;
    if (state == nullptr || state->entry == nullptr || state->context == nullptr) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotArtifactInvalid;
            error->message = "DirectNative generator state is incomplete";
        }
        return makeNil();
    }
    if (state->resume.ownershipTransferred) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotSwitchUnsupported;
            error->message = "DirectNative generator continuation ownership was transferred";
        }
        return makeNil();
    }
    if (state->done)
        return makeNil();
    if (memoryLimitReached(*state->context)) {
        memoryLimitExceeded(*state->context, error);
        state->done = true;
        state->resume.frame.reset();
        state->resume.injectionKind = DirectNativeInjectionKindV5::None;
        state->resume.injectionValue = makeNil();
        state->resume.awaitInjectionSupported = false;
        state->resume.awaitInjectionReady = false;
        state->initialArguments.clear();
        state->captures.clear();
        releaseGeneratorMemoryRoots(state);
        return makeNil();
    }
    const bool pendingAwaitInjection = state->resume.suspendKind == DirectNativeSuspendKindV5::Await
        && state->resume.awaitInjectionSupported;
    const bool pendingCollectionSend =
        state->resume.injectionKind == DirectNativeInjectionKindV5::Send;
    const Value* arguments = state->started || state->initialArguments.empty()
        ? nullptr
        : state->initialArguments.data();
    const std::size_t argumentCount = state->started ? 0u : state->initialArguments.size();
    state->started = true;
    state->resume.suspendKind = DirectNativeSuspendKindV5::None;
    const auto* previousCaptures = state->context->activeCaptures;
    state->context->activeCaptures = &state->captures;
    Value result;
    try {
        core::CpuMemoryQuotaScope quotaScope{state->context->memoryQuota};
        const ScriptValueSpan argumentRoots{arguments, argumentCount};
        if (!state->context->synchronizeMemoryRootsForOwner(
                state.get(), &argumentRoots, 1u, &state->captures, error)) {
            state->context->activeCaptures = previousCaptures;
            state->done = true;
            releaseGeneratorMemoryRoots(state);
            return makeNil();
        }
        result = state->entry(*state->context, arguments, argumentCount, &state->resume, error);
        const ScriptValueSpan resultRoot{&result, 1u};
        if ((error == nullptr || error->code == ScriptDiagnosticCode::None
                || error->code == ScriptDiagnosticCode::Suspended)
            && !state->context->synchronizeMemoryRootsForOwner(
                state.get(), &resultRoot, 1u, &state->captures, error))
            result = makeNil();
    } catch (const std::bad_alloc&) {
        state->context->activeCaptures = previousCaptures;
        state->resume.frame.reset();
        state->resume.injectionKind = DirectNativeInjectionKindV5::None;
        state->resume.injectionValue = makeNil();
        state->resume.awaitInjectionSupported = false;
        state->resume.awaitInjectionReady = false;
        state->initialArguments.clear();
        state->captures.clear();
        state->done = true;
        memoryLimitExceeded(*state->context, error);
        releaseGeneratorMemoryRoots(state);
        return makeNil();
    }
    state->context->activeCaptures = previousCaptures;
    state->initialArguments.clear();
    if (memoryLimitReached(*state->context)
        || (error != nullptr && error->code == ScriptDiagnosticCode::ResourceLimitExceeded)) {
        state->done = true;
        state->resume.frame.reset();
        state->resume.injectionKind = DirectNativeInjectionKindV5::None;
        state->resume.injectionValue = makeNil();
        state->resume.awaitInjectionSupported = false;
        state->resume.awaitInjectionReady = false;
        state->captures.clear();
        memoryLimitExceeded(*state->context, error);
        releaseGeneratorMemoryRoots(state);
        return makeNil();
    }
    const bool errorIsTerminal = error != nullptr && error->code != ScriptDiagnosticCode::None
        && error->code != ScriptDiagnosticCode::Suspended;
    const bool suspended = !errorIsTerminal
        && (state->resume.suspendKind != DirectNativeSuspendKindV5::None
            || state->resume.frame != nullptr
            || (error != nullptr && error->code == ScriptDiagnosticCode::Suspended));
    if (suspended) {
        if (state->resume.suspendKind == DirectNativeSuspendKindV5::Yield) {
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::None;
                error->message.clear();
            }
        }
        return result;
    }
    if (error != nullptr && error->code != ScriptDiagnosticCode::None) {
        // A rejected pending-await injection is a non-consuming protocol
        // failure. Keep the canonical await frame resumable so the caller can
        // retry or close it; terminal script/host failures still discard it.
        const bool rejectedCollectionSend =
            error->code == ScriptDiagnosticCode::AotSwitchUnsupported && pendingCollectionSend
            && error->message == "DirectNative yield-from collection accepts only nil send()";
        if (error->code == ScriptDiagnosticCode::AotSwitchUnsupported
            && (pendingAwaitInjection || rejectedCollectionSend)
            && state->resume.frame != nullptr) {
            state->resume.injectionKind = DirectNativeInjectionKindV5::None;
            state->resume.injectionValue = makeNil();
            state->resume.awaitInjectionReady = false;
            state->resume.suspendKind = pendingAwaitInjection ? DirectNativeSuspendKindV5::Await
                                                              : DirectNativeSuspendKindV5::Yield;
            return makeNil();
        }
        // An uncaught error is terminal for a DirectNative generator.  Do not
        // retain the frame at its failing PC: resuming it would replay the
        // failing operation or its side effect.
        state->done = true;
        state->resume.frame.reset();
        state->resume.injectionKind = DirectNativeInjectionKindV5::None;
        state->resume.injectionValue = makeNil();
        state->resume.awaitInjectionSupported = false;
        state->resume.awaitInjectionReady = false;
        state->captures.clear();
        releaseGeneratorMemoryRoots(state);
        return makeNil();
    }
    if (error == nullptr || error->code == ScriptDiagnosticCode::None) {
        state->done = true;
        state->resume.frame.reset();
        state->resume.injectionKind = DirectNativeInjectionKindV5::None;
        state->resume.injectionValue = makeNil();
        state->resume.awaitInjectionSupported = false;
        state->resume.awaitInjectionReady = false;
        state->captures.clear();
        releaseGeneratorMemoryRoots(state);
    }
    return result;
}

} // namespace

Value resumeDirectNativeGeneratorV5(DirectNativeGeneratorV5& generator, VmError* error)
{
    return resumeGenerator(generator, error);
}

Value sendDirectNativeGeneratorV5(
    DirectNativeGeneratorV5& generator, const Value& value, VmError* error)
{
    if (generator.state == nullptr || generator.state->context == nullptr
        || !generator.state->started
        || (generator.state->resume.suspendKind != DirectNativeSuspendKindV5::Yield
            && !(generator.state->resume.suspendKind == DirectNativeSuspendKindV5::Await
                && generator.state->resume.awaitInjectionSupported))) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotSwitchUnsupported;
            error->message = "DirectNative generator send requires a prior yield";
        }
        return makeNil();
    }
    try {
        core::CpuMemoryQuotaScope quotaScope{generator.state->context->memoryQuota};
        const ScriptValueSpan injectionRoot{&value, 1u};
        if (!generator.state->context->synchronizeMemoryRootsForOwner(
                generator.state.get(), &injectionRoot, 1u, &generator.state->captures, error))
            return makeNil();
        generator.state->resume.injectionKind = DirectNativeInjectionKindV5::Send;
        generator.state->resume.injectionValue = value;
    } catch (const std::bad_alloc&) {
        memoryLimitExceeded(*generator.state->context, error);
        return makeNil();
    }
    return resumeGenerator(generator, error);
}

Value throwDirectNativeGeneratorV5(
    DirectNativeGeneratorV5& generator, const Value& value, VmError* error)
{
    if (generator.state == nullptr || generator.state->context == nullptr
        || !generator.state->started
        || (generator.state->resume.suspendKind != DirectNativeSuspendKindV5::Yield
            && !(generator.state->resume.suspendKind == DirectNativeSuspendKindV5::Await
                && generator.state->resume.awaitInjectionSupported))) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotSwitchUnsupported;
            error->message = "DirectNative generator throw requires a prior yield";
        }
        return makeNil();
    }
    try {
        core::CpuMemoryQuotaScope quotaScope{generator.state->context->memoryQuota};
        const ScriptValueSpan injectionRoot{&value, 1u};
        if (!generator.state->context->synchronizeMemoryRootsForOwner(
                generator.state.get(), &injectionRoot, 1u, &generator.state->captures, error))
            return makeNil();
        generator.state->resume.injectionKind = DirectNativeInjectionKindV5::Throw;
        generator.state->resume.injectionValue = value;
    } catch (const std::bad_alloc&) {
        memoryLimitExceeded(*generator.state->context, error);
        return makeNil();
    }
    return resumeGenerator(generator, error);
}

void closeDirectNativeGeneratorV5(DirectNativeGeneratorV5& generator) noexcept
{
    if (generator.state == nullptr)
        return;
    generator.state->done = true;
    generator.state->resume.suspendKind = DirectNativeSuspendKindV5::None;
    generator.state->resume.awaitInjectionSupported = false;
    generator.state->resume.awaitInjectionReady = false;
    generator.state->resume.frame.reset();
    generator.state->resume.injectionKind = DirectNativeInjectionKindV5::None;
    generator.state->resume.injectionValue = makeNil();
    generator.state->initialArguments.clear();
    generator.state->captures.clear();
    if (generator.state->context != nullptr)
        generator.state->context->releaseMemoryRoots(generator.state.get());
}

DirectNativeGeneratorStateV5::~DirectNativeGeneratorStateV5()
{
    if (context != nullptr)
        context->releaseMemoryRoots(this);
}

bool DirectNativeExecutionContext::enterFunction(const char* functionId, VmError* error)
{
    if (memoryLimitReached(*this)) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    if (budget.recursionLimit == 0u || recursionDepth >= budget.recursionLimit) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::RecursionLimitExceeded;
            error->message = "DirectNative recursion budget exceeded";
            error->functionId = present(functionId) ? functionId : "<unknown>";
        }
        return false;
    }
    ++recursionDepth;
    return true;
}

bool DirectNativeExecutionContext::ensureGlobals(std::size_t count, VmError* error)
{
    if (memoryLimitReached(*this)) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    if (globals.empty()) {
        globals.resize(count);
        return true;
    }
    if (globals.size() == count) {
        return true;
    }
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::AotArtifactInvalid;
        error->message = "DirectNative module global layout changed";
    }
    return false;
}

ARP_API const GeneratorBridgeV2* optionalGeneratorBridgeV2() noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    return generatorBridgeV2 != nullptr ? generatorBridgeV2() : nullptr;
#else
    return nullptr;
#endif
}

ARP_API const GeneratorBridgeV3* optionalGeneratorBridgeV3() noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    return generatorBridgeV3 != nullptr ? generatorBridgeV3() : nullptr;
#else
    return nullptr;
#endif
}

ARP_API const GeneratorBridgeV3Capability* optionalGeneratorBridgeV3Capability() noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    return generatorBridgeV3Capability != nullptr ? generatorBridgeV3Capability() : nullptr;
#else
    return nullptr;
#endif
}

namespace {

bool hasSameOwner(const std::shared_ptr<void>& lhs, const std::shared_ptr<void>& rhs) noexcept
{
    // Pointer equality is insufficient for aliased/shared raw pointers. The
    // package lifetime is the shared ownership control block, including the
    // empty static-package owner case.
    return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
}

ScriptResult validateDirectNativeContinuationProvenance(
    const DirectNativeExecutionContext& context, const DirectNativeResumeStateV5& resume)
{
    if (resume.ownershipTransferred) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative continuation ownership was transferred"));
    }
    if (resume.commitState == 1u) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative continuation has an uncommitted side effect"));
    }
    if (resume.commitState > 2u) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotArtifactInvalid,
            text("DirectNative continuation commit state is invalid"));
    }
    if (resume.frame == nullptr || !resume.frame->hasCanonicalState()) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative continuation has no canonical operation-boundary state"));
    }
    const auto* module = context.moduleDescriptor;
    if (module == nullptr || context.moduleId.empty() || context.moduleVersion == 0u
        || context.artifactIdentity.empty() || context.moduleId != module->moduleId
        || context.moduleVersion != module->moduleVersion
        || context.artifactIdentity != module->artifactIdentity
        || resume.frame->functionId() == nullptr || resume.frame->functionIdentity() == nullptr) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
            text("DirectNative continuation identity is incomplete"));
    }
    const AotFunctionDescriptorV5* function = nullptr;
    for (std::size_t index = 0u; index < module->functionCount; ++index) {
        const auto& candidate = module->functions[index];
        if (candidate.functionId != nullptr
            && std::strcmp(candidate.functionId, resume.frame->functionId()) == 0) {
            function = &candidate;
            break;
        }
    }
    if (function == nullptr || function->functionIdentity == nullptr
        || std::strcmp(function->functionIdentity, resume.frame->functionIdentity()) != 0) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
            text("DirectNative continuation frame provenance mismatch"));
    }
    if (resume.pc != resume.frame->canonicalPc()) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotArtifactInvalid,
            text("DirectNative continuation pc does not match canonical state"));
    }
    return ScriptResult::success();
}

ScriptResult materializeDirectNativeSnapshot(const DirectNativeExecutionContext& context,
    const DirectNativeResumeStateV5& resume,
    SuspendedScriptFrame& out)
{
    const auto provenance = validateDirectNativeContinuationProvenance(context, resume);
    if (!provenance.ok())
        return provenance;
    try {
        core::CpuMemoryQuotaScope quotaScope{context.memoryQuota};
        SuspendedScriptFrame candidate;
        candidate.moduleId = context.moduleId;
        candidate.moduleVersion = context.moduleVersion;
        candidate.artifactIdentity = context.artifactIdentity;
        candidate.aotModule = context.moduleDescriptor;
        candidate.moduleOwner = context.packageOwner;
        candidate.functionId = resume.frame->functionId();
        candidate.safepointId = candidate.functionId;
        candidate.safepointId += ":pc:";
        candidate.safepointId += std::to_string(resume.pc);
        candidate.bytecodePc = static_cast<int>(resume.pc);
        candidate.locals = resume.frame->canonicalLocals();
        candidate.operandStack = resume.frame->canonicalStack();
        candidate.captureCells = resume.frame->canonicalCaptureCells();
        for (const auto& capture : candidate.captureCells)
            candidate.captures.push_back(capture ? capture->value : makeNil());
        candidate.handlerStack = resume.frame->canonicalHandlerPcs();
        candidate.handlerStackDepths = resume.frame->canonicalHandlerDepths();
        candidate.source = resume.frame->canonicalSource();
        candidate.awaiting = resume.frame->canonicalAwaiting();
        candidate.suspendedValue = resume.frame->canonicalSuspendedValue();
        candidate.delegatedActive = resume.frame->canonicalDelegatedActive();
        candidate.delegatedSourceSlot = resume.frame->canonicalDelegatedSourceSlot();
        candidate.delegatedRangePc = resume.frame->canonicalDelegatedRangePc();
        candidate.delegatedYieldPc = resume.frame->canonicalDelegatedYieldPc();
        candidate.delegatedChildAwaiting = resume.frame->canonicalDelegatedChildAwaiting();
        candidate.delegatedPendingYieldPc = resume.frame->canonicalDelegatedPendingYieldPc();
        const auto valid = validateSuspendedScriptFrame(candidate);
        if (!valid.ok())
            return valid;
        out = std::move(candidate);
        return ScriptResult::success();
    } catch (const std::bad_alloc&) {
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            text("DirectNative canonical snapshot allocation exceeded the memory budget"));
    }
}

} // namespace

ARP_API ScriptResult snapshotDirectNativeResumeStateV5(const DirectNativeExecutionContext& context,
    const DirectNativeResumeStateV5& resume,
    SuspendedScriptFrame& out)
{
    return materializeDirectNativeSnapshot(context, resume, out);
}

ARP_API ScriptResult transferDirectNativeResumeStateV5(DirectNativeExecutionContext& context,
    DirectNativeResumeStateV5& resume,
    SuspendedScriptFrame& out)
{
    SuspendedScriptFrame candidate;
    const auto materialized = materializeDirectNativeSnapshot(context, resume, candidate);
    if (!materialized.ok())
        return materialized;
    const auto sourceOwner = context.lifetimeOwner.lock();
    if (sourceOwner == nullptr || sourceOwner.get() != &context) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative continuation transfer requires an owned context"));
    }
    std::size_t chargedBytes = 0u;
    if (!resume.frame->continuationFrameCharge(context, chargedBytes) || chargedBytes == 0u) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative continuation has no transferable frame charge"));
    }
    try {
        core::CpuMemoryQuotaScope quotaScope{context.memoryQuota};
        auto ticket =
            core::makeShared<DirectNativeContinuationTransferV5>(sourceOwner, chargedBytes);
        if (!ticket->assumeMemoryRoots(resume.frame.get())) {
            return ScriptResult::failure(ScriptDiagnosticCode::AotArtifactInvalid,
                text("DirectNative continuation memory-root transfer failed"));
        }
        if (!resume.frame->relinquishContinuationFrameCharge(context, chargedBytes)) {
            (void)context.transferMemoryRoots(ticket.get(), resume.frame.get());
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                text("DirectNative continuation frame charge transfer failed"));
        }
        ticket->assumeSourceCharge();
        resume.frame->relinquishDelegatedChildForTransfer();
        resume.frame.reset();
        resume.pc = 0u;
        resume.commitState = 0u;
        resume.injectionKind = DirectNativeInjectionKindV5::None;
        resume.injectionValue = makeNil();
        resume.awaitInjectionSupported = false;
        resume.awaitInjectionReady = false;
        resume.suspendKind = DirectNativeSuspendKindV5::None;
        resume.ownershipTransferred = true;
        candidate.directNativeTransfer = std::move(ticket);
        out = std::move(candidate);
        return ScriptResult::success();
    } catch (const std::bad_alloc&) {
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            text("DirectNative continuation transfer allocation exceeded the memory budget"));
    }
}

ARP_API ScriptResult adoptDirectNativeSuspendedFrameV5(DirectNativeExecutionContext& context,
    const AotModuleDescriptorV5& module,
    const SuspendedScriptFrame& frame,
    DirectNativeResumeStateV5& resume)
{
    (void)context;
    (void)module;
    (void)frame;
    (void)resume;
    return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
        text("DirectNative canonical adoption requires a consumed transfer frame"));
}

ARP_API ScriptResult adoptDirectNativeSuspendedFrameV5(DirectNativeExecutionContext& context,
    const AotModuleDescriptorV5& module,
    SuspendedScriptFrame& frame,
    DirectNativeResumeStateV5& resume)
{
    const auto valid = validateSuspendedScriptFrame(frame);
    if (!valid.ok())
        return valid;
    if (frame.callers.size() != 0u) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative canonical adoption requires an empty caller chain"));
    }
    if (context.moduleDescriptor != &module || context.moduleId != module.moduleId
        || context.moduleVersion != module.moduleVersion
        || context.artifactIdentity != module.artifactIdentity || frame.moduleId != module.moduleId
        || frame.moduleVersion != module.moduleVersion
        || frame.artifactIdentity != module.artifactIdentity
        || !hasSameOwner(frame.moduleOwner, context.packageOwner)) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
            text("DirectNative canonical continuation module owner or identity mismatch"));
    }
    const AotFunctionDescriptorV5* function = nullptr;
    for (std::size_t index = 0u; index < module.functionCount; ++index) {
        if (module.functions[index].functionId != nullptr
            && frame.functionId == module.functions[index].functionId) {
            function = &module.functions[index];
            break;
        }
    }
    if (function == nullptr || function->entry == nullptr || function->functionIdentity == nullptr
        || frame.bytecodePc < 0 || frame.functionId != function->functionId
        || frame.artifactIdentity != module.artifactIdentity) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative canonical continuation function is unavailable"));
    }
    const bool operationSwitchable =
        (static_cast<std::uint32_t>(function->osrCapability)
            & static_cast<std::uint32_t>(AotOsrCapability::OperationBoundary))
        != 0u;
    if (!operationSwitchable && !frame.awaiting) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative canonical continuation is not operation-switchable"));
    }
    if (resume.frame != nullptr || resume.commitState != 0u) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative resume state already owns a continuation"));
    }
    if (frame.directNativeTransfer == nullptr || !frame.directNativeTransfer->claim()) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            text("DirectNative canonical continuation ownership is unavailable"));
    }
    VmError allocationError;
    if (!context.beginContinuationFrame(sizeof(DirectNativeCanonicalFrameV5), &allocationError)) {
        frame.directNativeTransfer->abandonClaim();
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            text("DirectNative canonical continuation frame budget exceeded"));
    }
    try {
        core::CpuMemoryQuotaScope quotaScope{context.memoryQuota};
        core::CpuMemoryQuotaLease frameLease;
        if (!frameLease.acquire(context.memoryQuota, sizeof(DirectNativeCanonicalFrameV5))) {
            frame.directNativeTransfer->abandonClaim();
            return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
                text("DirectNative canonical continuation frame budget exceeded"));
        }
        auto carrier =
            core::makeQuotaPrechargedUnique<DirectNativeCanonicalFrameV5>(std::move(frameLease),
                context,
                core::pmr::String(frame.functionId, &core::defaultCpuMemoryResource()),
                core::pmr::String(function->functionIdentity, &core::defaultCpuMemoryResource()));
        carrier->retainCharge();
        carrier->captureCanonicalState(static_cast<std::uint32_t>(frame.bytecodePc),
            frame.locals.data(),
            frame.locals.size(),
            frame.operandStack.data(),
            frame.operandStack.size(),
            frame.captureCells.data(),
            frame.captureCells.size(),
            frame.handlerStack.data(),
            frame.handlerStackDepths.data(),
            frame.handlerStack.size(),
            frame.awaiting,
            frame.suspendedValue);
        carrier->setCanonicalSource(
            core::pmr::String(frame.source.file, &core::defaultCpuMemoryResource()),
            frame.source.line,
            frame.source.column);
        if (frame.delegatedActive) {
            carrier->captureCanonicalDelegation(frame.delegatedSourceSlot,
                frame.delegatedRangePc,
                frame.delegatedYieldPc,
                frame.delegatedChildAwaiting,
                frame.delegatedPendingYieldPc);
        }
        const ScriptValueSpan carrierRoots[]{
            {carrier->canonicalLocals().data(), carrier->canonicalLocals().size()},
            {carrier->canonicalStack().data(), carrier->canonicalStack().size()},
            {&carrier->canonicalSuspendedValue(), 1u},
        };
        VmError rootError;
        if (!context.synchronizeMemoryRootsForOwner(carrier.get(),
                carrierRoots,
                sizeof(carrierRoots) / sizeof(carrierRoots[0]),
                &carrier->canonicalCaptureCells(),
                &rootError)) {
            frame.directNativeTransfer->abandonClaim();
            return ScriptResult::failure(rootError.code, std::move(rootError.message));
        }
        resume.pc = static_cast<std::uint32_t>(frame.bytecodePc);
        resume.commitState = 0u;
        resume.injectionKind = DirectNativeInjectionKindV5::None;
        resume.injectionValue = makeNil();
        resume.awaitInjectionReady = false;
        resume.awaitInjectionSupported = false;
        resume.ownershipTransferred = false;
        resume.suspendKind = frame.awaiting
            ? DirectNativeSuspendKindV5::Await
            : (frame.delegatedChildAwaiting
                      ? DirectNativeSuspendKindV5::Await
                      : (frame.delegatedActive ? DirectNativeSuspendKindV5::Yield
                                               : DirectNativeSuspendKindV5::None));
        if (frame.delegatedChildAwaiting) {
            const Value& child = frame.locals[static_cast<std::size_t>(frame.delegatedSourceSlot)];
            if (const auto* direct = asDirectNativeGeneratorV5(child);
                direct != nullptr && direct->state != nullptr) {
                resume.awaitInjectionSupported = direct->state->resume.awaitInjectionSupported;
            } else if (const auto* bridge = optionalGeneratorBridgeV3();
                bridge != nullptr && bridge->isGenerator != nullptr && bridge->isGenerator(child)) {
                const auto* capability = optionalGeneratorBridgeV3Capability();
                resume.awaitInjectionSupported = capability != nullptr
                    && capability->isGenerator != nullptr && capability->isGenerator(child)
                    && capability->supportsPendingAwaitInjection != nullptr
                    && capability->supportsPendingAwaitInjection(child);
            }
        }
        resume.frame = std::move(carrier);
        frame.directNativeTransfer->releaseMemoryRoots();
        frame.directNativeTransfer->releaseSourceCharge();
        frame = SuspendedScriptFrame{};
        return ScriptResult::success();
    } catch (const std::bad_alloc&) {
        frame.directNativeTransfer->abandonClaim();
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            text("DirectNative canonical continuation allocation exceeded the memory budget"));
    }
}

DirectNativeCanonicalFrameV5::~DirectNativeCanonicalFrameV5()
{
    if (owner_ != nullptr)
        owner_->releaseMemoryRoots(this);
    if (ownsCharge_ && owner_ != nullptr)
        owner_->releaseContinuationFrame(chargeBytes_);
}

bool DirectNativeCanonicalFrameV5::continuationFrameCharge(
    const DirectNativeExecutionContext& expected, std::size_t& outBytes) const noexcept
{
    if (!ownsCharge_ || owner_ != &expected)
        return false;
    outBytes = chargeBytes_;
    return outBytes != 0u;
}

bool DirectNativeCanonicalFrameV5::relinquishContinuationFrameCharge(
    DirectNativeExecutionContext& expected, std::size_t& outBytes) noexcept
{
    if (!continuationFrameCharge(expected, outBytes))
        return false;
    ownsCharge_ = false;
    owner_ = nullptr;
    return true;
}

void DirectNativeCanonicalFrameV5::retainCharge() noexcept
{
    if (owner_ == nullptr || ownsCharge_)
        return;
    chargeBytes_ = sizeof(DirectNativeCanonicalFrameV5);
    owner_->retainContinuationFrame(chargeBytes_);
    ownsCharge_ = true;
}

DirectNativeContinuationTransferV5::DirectNativeContinuationTransferV5(
    std::shared_ptr<DirectNativeExecutionContext> sourceOwner, std::size_t chargedBytes) noexcept
    : sourceOwner_(std::move(sourceOwner)), chargedBytes_(chargedBytes), sourceChargeLive_(false)
{}

DirectNativeContinuationTransferV5::~DirectNativeContinuationTransferV5()
{
    releaseMemoryRoots();
    releaseSourceCharge();
}

bool DirectNativeContinuationTransferV5::claim() noexcept
{
    bool expected = false;
    return claimed_.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

void DirectNativeContinuationTransferV5::abandonClaim() noexcept
{
    claimed_.store(false, std::memory_order_release);
}

void DirectNativeContinuationTransferV5::assumeSourceCharge() noexcept
{
    sourceChargeLive_ = true;
}

void DirectNativeContinuationTransferV5::releaseSourceCharge() noexcept
{
    if (!sourceChargeLive_)
        return;
    sourceChargeLive_ = false;
    if (sourceOwner_ != nullptr)
        sourceOwner_->releaseContinuationFrame(chargedBytes_);
}

bool DirectNativeContinuationTransferV5::assumeMemoryRoots(const void* sourceOwner) noexcept
{
    if (sourceOwner_ == nullptr)
        return false;
    if (!sourceOwner_->transferMemoryRoots(sourceOwner, this))
        return false;
    memoryRootsLive_ = true;
    return true;
}

void DirectNativeContinuationTransferV5::releaseMemoryRoots() noexcept
{
    if (!memoryRootsLive_)
        return;
    memoryRootsLive_ = false;
    if (sourceOwner_ != nullptr)
        sourceOwner_->releaseMemoryRoots(this);
}

bool DirectNativeExecutionContext::beginContinuationFrame(std::size_t frameBytes, VmError* error)
{
    (void)frameBytes;
    if (memoryLimitReached(*this)) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    if (continuationFrameCount >= budget.maxContinuationFrames) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::ResourceLimitExceeded;
            error->message = "DirectNative continuation frame budget exceeded";
        }
        return false;
    }
    return true;
}

bool DirectNativeExecutionContext::canReplaceContinuationFrame(
    std::size_t oldFrameBytes, std::size_t newFrameBytes, VmError* error) const
{
    if (oldFrameBytes > continuationFrameBytes) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotArtifactInvalid;
            error->message = "DirectNative continuation replacement charge is invalid";
        }
        return false;
    }
    (void)newFrameBytes;
    if (memoryLimitReached(*this)) {
        memoryLimitExceeded(error);
        return false;
    }
    return true;
}

void DirectNativeExecutionContext::replaceContinuationFrameCharge(
    std::size_t oldFrameBytes, std::size_t newFrameBytes) noexcept
{
    continuationFrameBytes =
        oldFrameBytes > continuationFrameBytes ? 0u : continuationFrameBytes - oldFrameBytes;
    const auto newBytes = static_cast<std::uint64_t>(newFrameBytes);
    if (newBytes > std::numeric_limits<std::uint64_t>::max() - continuationFrameBytes)
        continuationFrameBytes = std::numeric_limits<std::uint64_t>::max();
    else
        continuationFrameBytes += newBytes;
}

void DirectNativeExecutionContext::retainContinuationFrame(std::size_t frameBytes) noexcept
{
    ++continuationFrameCount;
    const auto bytes = static_cast<std::uint64_t>(frameBytes);
    if (bytes > std::numeric_limits<std::uint64_t>::max() - continuationFrameBytes)
        continuationFrameBytes = std::numeric_limits<std::uint64_t>::max();
    else
        continuationFrameBytes += bytes;
}

void DirectNativeExecutionContext::releaseContinuationFrame(std::size_t frameBytes) noexcept
{
    if (continuationFrameCount != 0u)
        --continuationFrameCount;
    const auto bytes = static_cast<std::uint64_t>(frameBytes);
    continuationFrameBytes = bytes > continuationFrameBytes ? 0u : continuationFrameBytes - bytes;
}

void DirectNativeExecutionContext::leaveFunction() noexcept
{
    if (recursionDepth != 0u) {
        --recursionDepth;
    }
}

bool DirectNativeExecutionContext::beginOperation(
    const char* functionId, std::uint32_t pc, DirectNativeResumeStateV5& resume, VmError* error)
{
    if (memoryLimitReached(*this)) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    if (resume.commitState == 2u && resume.pc == pc)
        resume.commitState = 0u;
    if (remainingInstructions == 0u) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::ExecutionBudgetExceeded;
            error->message = "DirectNative instruction budget exceeded";
            error->functionId = present(functionId) ? functionId : "<unknown>";
        }
        return false;
    }
    --remainingInstructions;
    resume.pc = pc;
    if (operationBoundary != nullptr
        && !operationBoundary(operationBoundaryUserData, functionId, pc, resume, error)) {
        if (error != nullptr && error->code == ScriptDiagnosticCode::None) {
            error->code = ScriptDiagnosticCode::Suspended;
            error->message = "DirectNative operation-boundary migration requested";
            error->functionId = present(functionId) ? functionId : "<unknown>";
        }
        return false;
    }
    return true;
}

ScriptResult validatePluginDependency(const AotPluginDependencyV5& dependency)
{
    if (!present(dependency.moduleId) || dependency.moduleVersion == 0u
        || !validSha256(dependency.exportContractHash)
        || !validSha256(dependency.reflectionRegistryHash)
        || (dependency.serviceCount != 0u && dependency.services == nullptr)) {
        return invalid("AOT v5 Plugin dependency contract is incomplete");
    }
    for (std::size_t index = 0; index < dependency.serviceCount; ++index) {
        const auto& service = dependency.services[index];
        if (!present(service.serviceId) || service.version == 0u) {
            return invalid("AOT v5 Plugin service dependency is incomplete");
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (std::strcmp(dependency.services[other].serviceId, service.serviceId) == 0) {
                return invalid("AOT v5 Plugin service dependency is duplicated");
            }
        }
    }
    return ScriptResult::success();
}

bool DirectNativeExecutionContext::invokeBoundCall(const char* bindingId,
    const char* targetId,
    const Value* receiver,
    const Value* arguments,
    std::size_t argumentCount,
    Value& result,
    VmError* error)
{
    if (memoryLimitReached(*this)) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    if (argumentCount > budget.maxCallArguments) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::ResourceLimitExceeded;
            error->message = "DirectNative bound call exceeds the argument limit";
        }
        return false;
    }
    if (!present(bindingId) || !present(targetId) || boundCall == nullptr) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotModuleUnavailable;
            error->message = "DirectNative bound callable is unavailable";
        }
        return false;
    }
    MemoryRootOwnerScope memoryRoots{*this};
    const ScriptValueSpan argumentRoots[]{
        {receiver, receiver != nullptr ? 1u : 0u},
        {arguments, argumentCount},
    };
    if (!memoryRoots.synchronize(argumentRoots,
            sizeof(argumentRoots) / sizeof(argumentRoots[0]),
            activeCaptures,
            error)) {
        return false;
    }
    const bool succeeded = boundCall(
        boundCallUserData, bindingId, targetId, receiver, arguments, argumentCount, &result, error);
    if (error != nullptr && error->code == ScriptDiagnosticCode::ResourceLimitExceeded) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    const ScriptValueSpan resultRoot{&result, 1u};
    return succeeded && memoryRoots.synchronize(&resultRoot, 1u, activeCaptures, error);
}

bool DirectNativeExecutionContext::resolveModuleExport(
    const char* moduleId, const char* exportName, Value& result, VmError* error)
{
    if (memoryLimitReached(*this)) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    if (moduleExport == nullptr) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotModuleUnavailable;
            error->message = "DirectNative module export resolver is not configured";
        }
        return false;
    }
    MemoryRootOwnerScope memoryRoots{*this};
    const bool succeeded = moduleExport(moduleExportUserData, moduleId, exportName, &result, error);
    if (error != nullptr && error->code == ScriptDiagnosticCode::ResourceLimitExceeded) {
        memoryLimitExceeded(*this, error);
        return false;
    }
    const ScriptValueSpan resultRoot{&result, 1u};
    return succeeded && memoryRoots.synchronize(&resultRoot, 1u, activeCaptures, error);
}

bool DirectNativeExecutionContext::acceptsGeneratorIdentity(
    const GeneratorIdentityV3& identity) const noexcept
{
    if (!identity.complete())
        return false;
    if (identity.moduleVersion == moduleVersion
        && std::strcmp(identity.moduleId, moduleId.c_str()) == 0
        && std::strcmp(identity.artifactIdentity, artifactIdentity.c_str()) == 0)
        return true;
    if (moduleDescriptor == nullptr || packageDescriptor == nullptr
        || moduleDescriptor->dependencies == nullptr)
        return false;
    for (std::size_t index = 0u; index < moduleDescriptor->dependencyCount; ++index) {
        const auto& dependency = moduleDescriptor->dependencies[index];
        if (dependency.moduleId == nullptr || dependency.artifactIdentity == nullptr)
            continue;
        if (dependency.moduleVersion == identity.moduleVersion
            && std::strcmp(dependency.moduleId, identity.moduleId) == 0
            && std::strcmp(dependency.artifactIdentity, identity.artifactIdentity) == 0)
            return true;
    }
    return false;
}

bool DirectNativeExecutionContext::synchronizeMemoryRoots(const ScriptValueSpan* spans,
    std::size_t spanCount,
    const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
    VmError* error)
{
    return synchronizeMemoryRootsForOwner(this, spans, spanCount, captures, error);
}

bool DirectNativeExecutionContext::synchronizeMemoryRootsForOwner(const void* owner,
    const ScriptValueSpan* spans,
    std::size_t spanCount,
    const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
    VmError* error)
{
    if (memoryQuota == nullptr) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotArtifactInvalid;
            error->message = "DirectNative memory quota is unavailable";
        }
        return false;
    }
    if (memoryAccountant == nullptr) {
        try {
            core::CpuMemoryQuotaScope accountingScope{nullptr};
            memoryAccountant = core::makeShared<ScriptValueMemoryAccountant>(memoryQuota);
        } catch (const std::bad_alloc&) {
            memoryLimitExceeded(*this, error);
            return false;
        }
    }
    std::size_t maxVisitedValues = budget.maxFrameValues;
    if (budget.maxContainerValues > std::numeric_limits<std::size_t>::max() - maxVisitedValues) {
        maxVisitedValues = std::numeric_limits<std::size_t>::max();
    } else {
        maxVisitedValues += budget.maxContainerValues;
    }
    if (!memoryAccountant->synchronizeOwner(&globalMemoryRootOwnerToken,
            globals,
            nullptr,
            0u,
            nullptr,
            maxVisitedValues,
            valueMemoryDescriptor,
            valueMemoryDescriptorUserData,
            error)) {
        if (error != nullptr && error->code == ScriptDiagnosticCode::ResourceLimitExceeded)
            memoryLimitExceeded(*this, error);
        return false;
    }
    if (!memoryAccountant->synchronizeOwner(owner,
            globals,
            spans,
            spanCount,
            captures,
            maxVisitedValues,
            valueMemoryDescriptor,
            valueMemoryDescriptorUserData,
            error)) {
        if (error != nullptr && error->code == ScriptDiagnosticCode::ResourceLimitExceeded)
            memoryLimitExceeded(*this, error);
        return false;
    }
    return true;
}

void DirectNativeExecutionContext::releaseMemoryRoots(const void* owner) noexcept
{
    if (memoryAccountant != nullptr)
        memoryAccountant->releaseOwner(owner);
}

bool DirectNativeExecutionContext::transferMemoryRoots(
    const void* sourceOwner, const void* targetOwner) noexcept
{
    return memoryAccountant == nullptr || memoryAccountant->transferOwner(sourceOwner, targetOwner);
}

void DirectNativeExecutionContext::prepareSideEffect(
    DirectNativeResumeStateV5& resume, std::uint32_t pc) noexcept
{
    resume.pc = pc;
    resume.commitState = 1u;
}

void DirectNativeExecutionContext::commitSideEffect(
    DirectNativeResumeStateV5& resume, std::uint32_t pc) noexcept
{
    resume.pc = pc + 1u;
    resume.commitState = 2u;
}

AotPlatform currentAotPlatform() noexcept
{
#if defined(__EMSCRIPTEN__)
    return AotPlatform::WebWasm;
#elif defined(_WIN32)
    return AotPlatform::Windows;
#elif defined(__APPLE__)
    return AotPlatform::MacOS;
#else
    return AotPlatform::None;
#endif
}

ScriptResult validateAotModuleV5(const AotModuleDescriptorV5& module, AotPlatform requiredPlatform)
{
    if (module.contractVersion != kAotContractVersion) {
        return versionMismatch("strict AOT loader accepts contract v5 only");
    }
    if (!present(module.packageIdentity) || !present(module.moduleId) || module.moduleVersion == 0u
        || !present(module.artifactIdentity)) {
        return invalid("AOT v5 module identity is incomplete");
    }
    if (!validPlatformMask(module.platforms)) {
        return invalid("AOT v5 module platform mask is invalid");
    }
    if (!validSha256(module.sourceHash) || !validSha256(module.dependencyGraphHash)
        || !validSha256(module.reflectionRegistryHash) || !validSha256(module.hostContractHash)
        || !validSha256(module.exportContractHash)) {
        return invalid("AOT v5 module requires complete sha256 contract hashes");
    }
    if (module.semanticAbiVersion == 0u || module.entryAbiVersion != kDirectNativeEntryAbiVersion) {
        return versionMismatch("AOT v5 module ABI is unsupported");
    }
    if (requiredPlatform == AotPlatform::None
        || !hasAotPlatform(module.platforms, requiredPlatform)) {
        return invalid("AOT v5 module does not target the required platform");
    }
    if (!explicitBudget(module.budget)) {
        return invalid("AOT v5 module requires explicit non-zero budgets");
    }
    if (module.functions == nullptr || module.functionCount == 0u) {
        return invalid("AOT v5 module has no DirectNative functions");
    }
    if (module.globalCount != 0u && !present(module.moduleInitFunctionId)) {
        return invalid("AOT v5 module globals require an initializer identity");
    }
    if (module.dependencyCount != 0u && module.dependencies == nullptr) {
        return invalid("AOT v5 module dependency table is missing");
    }
    if (module.pluginDependencyCount != 0u && module.pluginDependencies == nullptr) {
        return invalid("AOT v5 module Plugin dependency table is missing");
    }
    if (module.exportCount != 0u && module.exports == nullptr)
        return invalid("AOT v5 module export table is missing");
    for (std::size_t index = 0; index < module.exportCount; ++index) {
        if (!present(module.exports[index].name)
            || module.exports[index].globalSlot >= module.globalCount) {
            return invalid("AOT v5 module export is invalid");
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (std::strcmp(module.exports[other].name, module.exports[index].name) == 0) {
                return invalid("AOT v5 module exports are duplicated");
            }
        }
    }
    for (std::size_t index = 0; index < module.functionCount; ++index) {
        const auto functionResult = validateFunction(module.functions[index]);
        if (!functionResult.ok()) {
            return functionResult;
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (std::strcmp(module.functions[other].functionId, module.functions[index].functionId)
                    == 0
                || std::strcmp(module.functions[other].functionIdentity,
                       module.functions[index].functionIdentity)
                    == 0) {
                return invalid("AOT v5 module contains duplicate function identity");
            }
        }
    }
    if (present(module.moduleInitFunctionId)) {
        bool foundInitializer = false;
        for (std::size_t index = 0; index < module.functionCount; ++index) {
            foundInitializer = foundInitializer
                || std::strcmp(module.functions[index].functionId, module.moduleInitFunctionId)
                    == 0;
        }
        if (!foundInitializer) {
            return invalid("AOT v5 module initializer entry is missing");
        }
    }
    for (std::size_t index = 0; index < module.dependencyCount; ++index) {
        const auto& dependency = module.dependencies[index];
        if (!present(dependency.moduleId) || dependency.moduleVersion == 0u
            || !present(dependency.artifactIdentity)
            || !validSha256(dependency.exportContractHash)) {
            return invalid("AOT v5 module dependency contract is incomplete");
        }
    }
    for (std::size_t index = 0; index < module.pluginDependencyCount; ++index) {
        const auto result = validatePluginDependency(module.pluginDependencies[index]);
        if (!result.ok()) {
            return result;
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (std::strcmp(module.pluginDependencies[other].moduleId,
                    module.pluginDependencies[index].moduleId)
                == 0) {
                return invalid("AOT v5 module Plugin dependencies are duplicated");
            }
        }
    }
    return ScriptResult::success();
}

ScriptResult validateAotPackageV5(
    const AotPackageDescriptorV5& package, AotPlatform requiredPlatform)
{
    if (package.contractVersion != kAotContractVersion) {
        return versionMismatch("strict AOT package loader accepts contract v5 only");
    }
    if (!present(package.manifestType)
        || std::strcmp(package.manifestType, kAotPackageManifestType) != 0
        || !present(package.packageId) || package.packageVersion == 0u
        || !present(package.packageIdentity) || !validSha256(package.manifestHash)) {
        return invalid("AOT v5 package identity is incomplete");
    }
    if (!validPlatformMask(package.platforms)) {
        return invalid("AOT v5 package platform mask is invalid");
    }
    if (requiredPlatform == AotPlatform::None
        || !hasAotPlatform(package.platforms, requiredPlatform)) {
        return invalid("AOT v5 package does not target the required platform");
    }
    if (package.modules == nullptr || package.moduleCount == 0u) {
        return invalid("AOT v5 package has no modules");
    }
    for (std::size_t index = 0; index < package.moduleCount; ++index) {
        const auto& module = package.modules[index];
        if (!present(module.packageIdentity)
            || std::strcmp(module.packageIdentity, package.packageIdentity) != 0) {
            return invalid("AOT v5 module package identity does not match");
        }
        const auto moduleResult = validateAotModuleV5(module, requiredPlatform);
        if (!moduleResult.ok()) {
            return moduleResult;
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (std::strcmp(package.modules[other].moduleId, module.moduleId) == 0) {
                return invalid("AOT v5 package contains duplicate module identity");
            }
        }
        for (std::size_t dependencyIndex = 0; dependencyIndex < module.dependencyCount;
            ++dependencyIndex) {
            const auto& dependency = module.dependencies[dependencyIndex];
            const AotModuleDescriptorV5* target = nullptr;
            for (std::size_t candidate = 0; candidate < package.moduleCount; ++candidate) {
                if (std::strcmp(package.modules[candidate].moduleId, dependency.moduleId) == 0) {
                    target = &package.modules[candidate];
                    break;
                }
            }
            if (target == nullptr || target->moduleVersion != dependency.moduleVersion
                || std::strcmp(target->artifactIdentity, dependency.artifactIdentity) != 0
                || std::strcmp(target->exportContractHash, dependency.exportContractHash) != 0) {
                return invalid("AOT v5 package dependency identity does not resolve");
            }
        }
    }
    return ScriptResult::success();
}

ScriptResult validateAotPackageManifestV5(
    const AotPackageManifestV5& manifest, AotPlatform requiredPlatform)
{
    if (manifest.contractVersion != kAotContractVersion) {
        return versionMismatch("strict AOT manifest parser accepts contract v5 only");
    }
    if (manifest.type != kAotPackageManifestType || manifest.packageId.empty()
        || manifest.packageVersion == 0u || manifest.packageIdentity.empty()
        || !validSha256(manifest.manifestHash.c_str())
        || !hasAotPlatform(manifest.platforms, requiredPlatform) || manifest.modules.empty()) {
        return invalid("AOT v5 package manifest is incomplete");
    }
    if (!validPlatformMask(manifest.platforms)) {
        return invalid("AOT v5 package manifest platform mask is invalid");
    }
    for (std::size_t index = 0; index < manifest.modules.size(); ++index) {
        const auto moduleResult = validateManifestModule(
            manifest.modules[index], manifest.packageIdentity, requiredPlatform);
        if (!moduleResult.ok()) {
            return moduleResult;
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (manifest.modules[other].moduleId == manifest.modules[index].moduleId) {
                return invalid("AOT v5 manifest contains duplicate module identity");
            }
        }
    }
    return ScriptResult::success();
}

ScriptResult ScriptAotPackageRegistry::registerPackage(
    const AotPackageDescriptorV5& package, std::shared_ptr<void> owner)
{
    const auto validation = validateAotPackageV5(package, currentAotPlatform());
    if (!validation.ok()) {
        return validation;
    }
    for (std::size_t moduleIndex = 0; moduleIndex < package.moduleCount; ++moduleIndex) {
        const auto& module = package.modules[moduleIndex];
        if (module.pluginDependencyCount != 0u && pluginDependencyResolver_ == nullptr) {
            return ScriptResult::failure(ScriptDiagnosticCode::AotModuleUnavailable,
                text("AOT v5 package requires a native Plugin dependency resolver"));
        }
        for (std::size_t dependencyIndex = 0; dependencyIndex < module.pluginDependencyCount;
            ++dependencyIndex) {
            VmError error;
            const auto& dependency = module.pluginDependencies[dependencyIndex];
            if (!pluginDependencyResolver_(pluginDependencyResolverUserData_, dependency, &error)) {
                if (error.code == ScriptDiagnosticCode::None) {
                    error.code = ScriptDiagnosticCode::AotModuleUnavailable;
                    error.message = "AOT v5 native Plugin dependency was rejected";
                }
                return ScriptResult::failure(
                    ScriptDiagnostic{error.code, std::move(error.message)});
            }
        }
    }
    if (findPackage(package.packageIdentity) != nullptr) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotDuplicateEntry,
            text("AOT v5 package identity is already registered"));
    }
    entries_.push_back({&package, std::move(owner)});
    return ScriptResult::success();
}

const AotPackageDescriptorV5* ScriptAotPackageRegistry::findPackage(
    const char* packageIdentity) const
{
    if (!present(packageIdentity)) {
        return nullptr;
    }
    for (const auto& entry : entries_) {
        if (std::strcmp(entry.package->packageIdentity, packageIdentity) == 0) {
            return entry.package;
        }
    }
    return nullptr;
}

const AotModuleDescriptorV5* ScriptAotPackageRegistry::findModule(
    const char* packageIdentity, const char* moduleId) const
{
    const auto* package = findPackage(packageIdentity);
    if (package == nullptr || !present(moduleId)) {
        return nullptr;
    }
    for (std::size_t index = 0; index < package->moduleCount; ++index) {
        if (std::strcmp(package->modules[index].moduleId, moduleId) == 0) {
            return &package->modules[index];
        }
    }
    return nullptr;
}

std::shared_ptr<void> ScriptAotPackageRegistry::findOwner(const char* packageIdentity) const
{
    if (!present(packageIdentity))
        return {};
    for (const auto& entry : entries_) {
        if (std::strcmp(entry.package->packageIdentity, packageIdentity) == 0)
            return entry.owner;
    }
    return {};
}

bool ScriptAotPackageRegistry::registerCallback(
    const AotPackageDescriptorV5* package, void* userData)
{
    if (package == nullptr || userData == nullptr) {
        return false;
    }
    return static_cast<ScriptAotPackageRegistry*>(userData)->registerPackage(*package).ok();
}

AotRegistrationHostV5 ScriptAotPackageRegistry::registrationHost()
{
    return {kAotContractVersion, &registerCallback, this};
}

class DirectNativePackageRuntimeV5 final
    : public std::enable_shared_from_this<DirectNativePackageRuntimeV5> {
private:
    struct CallbackBridge final {
        std::weak_ptr<DirectNativePackageRuntimeV5> runtime;

        static bool unavailable(VmError* error) noexcept
        {
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::AotModuleUnavailable;
                try {
                    error->message = "DirectNative package runtime owner is no longer available";
                } catch (const std::bad_alloc&) {
                    error->message.clear();
                }
            }
            return false;
        }

        static bool boundCall(void* userData,
            const char* bindingId,
            const char* targetId,
            const Value* receiver,
            const Value* arguments,
            std::size_t argumentCount,
            Value* result,
            VmError* error)
        {
            auto* bridge = static_cast<CallbackBridge*>(userData);
            if (bridge == nullptr)
                return unavailable(error);
            const auto runtime = bridge->runtime.lock();
            if (runtime == nullptr)
                return unavailable(error);
            return runtime->invoke(
                bindingId, targetId, receiver, arguments, argumentCount, result, error);
        }

        static bool moduleExport(void* userData,
            const char* moduleId,
            const char* exportName,
            Value* result,
            VmError* error)
        {
            auto* bridge = static_cast<CallbackBridge*>(userData);
            if (bridge == nullptr)
                return unavailable(error);
            const auto runtime = bridge->runtime.lock();
            if (runtime == nullptr)
                return unavailable(error);
            return runtime->resolveExport(moduleId, exportName, result, error);
        }
    };

public:
    explicit DirectNativePackageRuntimeV5(const AotPackageDescriptorV5& package)
        : package_(&package)
    {}

    ScriptResult initialize()
    {
        contexts_.reserve(package_->moduleCount);
        initializing_.resize(package_->moduleCount, 0u);
        for (std::size_t index = 0; index < package_->moduleCount; ++index) {
            const auto& module = package_->modules[index];
            auto context = core::makeShared<DirectNativeExecutionContext>();
            context->moduleId = module.moduleId;
            context->moduleVersion = module.moduleVersion;
            context->artifactIdentity = module.artifactIdentity;
            context->packageDescriptor = package_;
            context->moduleDescriptor = &module;
            context->memoryQuota =
                core::makeShared<core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
            context->packageOwner = packageOwner_;
            context->lifetimeOwner = context;
            context->resetBudget(module.budget);
            auto callbackBridge = core::makeShared<CallbackBridge>();
            callbackBridge->runtime = shared_from_this();
            context->callbackOwner = callbackBridge;
            context->boundCall = &CallbackBridge::boundCall;
            context->boundCallUserData = callbackBridge.get();
            context->moduleExport = &CallbackBridge::moduleExport;
            context->moduleExportUserData = callbackBridge.get();
            VmError error;
            try {
                core::CpuMemoryQuotaScope quotaScope{context->memoryQuota};
                if (!context->ensureGlobals(module.globalCount, &error)) {
                    return ScriptResult::failure(
                        ScriptDiagnostic{error.code, std::move(error.message)});
                }
            } catch (const std::bad_alloc&) {
                memoryLimitExceeded(&error);
                return ScriptResult::failure(
                    ScriptDiagnostic{error.code, std::move(error.message)});
            }
            contexts_.push_back(std::move(context));
        }
        return ScriptResult::success();
    }

    std::shared_ptr<DirectNativeExecutionContext> contextFor(
        const AotModuleDescriptorV5* module) const
    {
        if (module == nullptr)
            return {};
        for (std::size_t index = 0; index < package_->moduleCount; ++index) {
            if (&package_->modules[index] == module)
                return contexts_[index];
        }
        return {};
    }

    void setPackageOwner(std::shared_ptr<void> owner)
    {
        packageOwner_ = std::move(owner);
        for (const auto& context : contexts_)
            context->packageOwner = packageOwner_;
    }

    void resetBudgets() noexcept
    {
        for (std::size_t index = 0; index < contexts_.size(); ++index)
            contexts_[index]->resetBudget(package_->modules[index].budget);
    }

    void configureFallback(DirectNativeBoundCallV5 callback, void* userData) noexcept
    {
        fallback_ = callback;
        fallbackUserData_ = userData;
    }

    void configureExportFallback(DirectNativeModuleExportV5 callback, void* userData) noexcept
    {
        exportFallback_ = callback;
        exportFallbackUserData_ = userData;
    }

    void configureValueMemoryDescriptor(
        ScriptValueMemoryDescriptorFn descriptor, void* userData) noexcept
    {
        for (const auto& context : contexts_) {
            context->valueMemoryDescriptor = descriptor;
            context->valueMemoryDescriptorUserData = userData;
        }
    }

private:
    bool ensureInitialized(std::size_t moduleIndex, VmError* error)
    {
        auto& context = *contexts_[moduleIndex];
        const auto& module = package_->modules[moduleIndex];
        if (context.memoryBudgetFailed) {
            memoryLimitExceeded(error);
            return false;
        }
        if (context.moduleInitialized)
            return true;
        if (initializing_[moduleIndex] != 0u) {
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::AotArtifactInvalid;
                error->message = "AOT v5 package has a cyclic module initializer";
            }
            return false;
        }
        if (!present(module.moduleInitFunctionId)) {
            context.moduleInitialized = true;
            return true;
        }
        const AotFunctionDescriptorV5* initializer = nullptr;
        for (std::size_t index = 0; index < module.functionCount; ++index) {
            if (std::strcmp(module.functions[index].functionId, module.moduleInitFunctionId) == 0) {
                initializer = &module.functions[index];
                break;
            }
        }
        if (initializer == nullptr) {
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::AotMissingEntry;
                error->message = "AOT v5 module initializer is missing";
            }
            return false;
        }
        initializing_[moduleIndex] = 1u;
        try {
            core::CpuMemoryQuotaScope quotaScope{context.memoryQuota};
            MemoryRootOwnerScope initializerRoots{context};
            if (!initializerRoots.synchronize(nullptr, 0u, nullptr, error)) {
                initializing_[moduleIndex] = 0u;
                return false;
            }
            (void)initializer->entry(context, nullptr, 0u, nullptr, error);
            if ((error == nullptr || error->code == ScriptDiagnosticCode::None)
                && !initializerRoots.synchronize(nullptr, 0u, nullptr, error)) {
                initializing_[moduleIndex] = 0u;
                return false;
            }
        } catch (const std::bad_alloc&) {
            memoryLimitExceeded(context, error);
        }
        if (error != nullptr && error->code == ScriptDiagnosticCode::ResourceLimitExceeded)
            memoryLimitExceeded(context, error);
        initializing_[moduleIndex] = 0u;
        if (error != nullptr && error->code != ScriptDiagnosticCode::None)
            return false;
        context.moduleInitialized = true;
        return true;
    }

    bool invoke(const char* bindingId,
        const char* targetId,
        const Value* receiver,
        const Value* arguments,
        std::size_t argumentCount,
        Value* result,
        VmError* error)
    {
        if (!present(targetId) || result == nullptr) {
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::AotArtifactInvalid;
                error->message = "AOT v5 bound call identity is incomplete";
            }
            return false;
        }
        if (std::strncmp(targetId, "arp-fn:", 7u) != 0) {
            if (fallback_ != nullptr) {
                return fallback_(fallbackUserData_,
                    bindingId,
                    targetId,
                    receiver,
                    arguments,
                    argumentCount,
                    result,
                    error);
            }
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::AotMissingEntry;
                error->message = "AOT v5 host/reflection bound call is not configured";
            }
            return false;
        }
        for (std::size_t moduleIndex = 0; moduleIndex < package_->moduleCount; ++moduleIndex) {
            const auto& module = package_->modules[moduleIndex];
            for (std::size_t functionIndex = 0; functionIndex < module.functionCount;
                ++functionIndex) {
                const auto& function = module.functions[functionIndex];
                if (std::strcmp(function.functionId, targetId) != 0)
                    continue;
                if (!ensureInitialized(moduleIndex, error))
                    return false;
                auto& context = *contexts_[moduleIndex];
                MemoryRootOwnerScope invocationRoots{context};
                try {
                    if (argumentCount > context.budget.maxCallArguments) {
                        if (error != nullptr) {
                            error->code = ScriptDiagnosticCode::ResourceLimitExceeded;
                            error->message = "DirectNative target call exceeds the argument limit";
                        }
                        *result = makeNil();
                        return false;
                    }
                    core::CpuMemoryQuotaScope quotaScope{context.memoryQuota};
                    const ScriptValueSpan argumentRoots{arguments, argumentCount};
                    if (!invocationRoots.synchronize(
                            &argumentRoots, 1u, context.activeCaptures, error)) {
                        *result = makeNil();
                        return false;
                    }
                    *result = function.entry(context, arguments, argumentCount, nullptr, error);
                    const ScriptValueSpan resultRoot{result, 1u};
                    if ((error == nullptr || error->code == ScriptDiagnosticCode::None)
                        && !invocationRoots.synchronize(
                            &resultRoot, 1u, context.activeCaptures, error)) {
                        *result = makeNil();
                        return false;
                    }
                } catch (const std::bad_alloc&) {
                    memoryLimitExceeded(*contexts_[moduleIndex], error);
                    *result = makeNil();
                    return false;
                }
                if (error != nullptr
                    && error->code == ScriptDiagnosticCode::ResourceLimitExceeded) {
                    memoryLimitExceeded(*contexts_[moduleIndex], error);
                    *result = makeNil();
                    return false;
                }
                return error == nullptr || error->code == ScriptDiagnosticCode::None;
            }
        }
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotMissingEntry;
            error->message = "AOT v5 script bound-call target is not registered";
        }
        return false;
    }

    bool resolveExport(const char* moduleId, const char* exportName, Value* result, VmError* error)
    {
        if (!present(moduleId) || !present(exportName) || result == nullptr) {
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::AotArtifactInvalid;
                error->message = "AOT v5 module export identity is incomplete";
            }
            return false;
        }
        for (std::size_t moduleIndex = 0; moduleIndex < package_->moduleCount; ++moduleIndex) {
            const auto& module = package_->modules[moduleIndex];
            if (std::strcmp(module.moduleId, moduleId) != 0)
                continue;
            if (!ensureInitialized(moduleIndex, error))
                return false;
            for (std::size_t exportIndex = 0; exportIndex < module.exportCount; ++exportIndex) {
                if (memoryLimitReached(*contexts_[moduleIndex])) {
                    memoryLimitExceeded(*contexts_[moduleIndex], error);
                    return false;
                }
                const auto& exported = module.exports[exportIndex];
                if (std::strcmp(exported.name, exportName) != 0)
                    continue;
                *result = contexts_[moduleIndex]->globals[exported.globalSlot];
                return true;
            }
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::AotMissingEntry;
                error->message = "AOT v5 module export is not registered";
            }
            return false;
        }
        if (exportFallback_ != nullptr) {
            return exportFallback_(exportFallbackUserData_, moduleId, exportName, result, error);
        }
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotModuleUnavailable;
            error->message = "AOT v5 external module export resolver is not configured";
        }
        return false;
    }

    const AotPackageDescriptorV5* package_ = nullptr;
    core::pmr::Vector<std::shared_ptr<DirectNativeExecutionContext>> contexts_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> initializing_{&core::defaultCpuMemoryResource()};
    DirectNativeBoundCallV5 fallback_ = nullptr;
    void* fallbackUserData_ = nullptr;
    DirectNativeModuleExportV5 exportFallback_ = nullptr;
    void* exportFallbackUserData_ = nullptr;
    std::shared_ptr<void> packageOwner_;
};

ScriptResult createAotOnlyModuleInstanceV5(const ScriptAotPackageRegistry& registry,
    const char* packageIdentity,
    const char* moduleId,
    ScriptAotModuleInstanceV5& output)
{
    output = {};
    const auto* package = registry.findPackage(packageIdentity);
    const auto* module = registry.findModule(packageIdentity, moduleId);
    if (package == nullptr || module == nullptr) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotModuleUnavailable,
            text("AotOnly v5 package/module identity is not registered"));
    }
    const auto validation = validateAotModuleV5(*module, currentAotPlatform());
    if (!validation.ok()) {
        return validation;
    }
    output.packageIdentity = packageIdentity;
    output.moduleId = moduleId;
    output.module = module;
    output.package = package;
    output.packageOwner = registry.findOwner(packageIdentity);
    output.packageRuntime = core::makeShared<DirectNativePackageRuntimeV5>(*package);
    // The initializer is executable generated code too. Propagate the DLL
    // owner before initialization so a closure/generator created by the
    // initializer cannot capture a context that outlives the loaded module.
    output.packageRuntime->setPackageOwner(output.packageOwner);
    const auto initialized = output.packageRuntime->initialize();
    if (!initialized.ok()) {
        output = {};
        return initialized;
    }
    output.context = output.packageRuntime->contextFor(module);
    return ScriptResult::success();
}

ScriptResult configureAotOnlyBoundCallV5(
    ScriptAotModuleInstanceV5& instance, DirectNativeBoundCallV5 callback, void* userData)
{
    if (instance.packageRuntime == nullptr)
        return invalid("AOT v5 package runtime is not initialized");
    instance.packageRuntime->configureFallback(callback, userData);
    return ScriptResult::success();
}

ScriptResult configureAotOnlyModuleExportV5(
    ScriptAotModuleInstanceV5& instance, DirectNativeModuleExportV5 callback, void* userData)
{
    if (instance.packageRuntime == nullptr)
        return invalid("AOT v5 package runtime is not initialized");
    instance.packageRuntime->configureExportFallback(callback, userData);
    return ScriptResult::success();
}

DirectNativeDispatcherV5::DirectNativeDispatcherV5(ScriptAotModuleInstanceV5 module)
    : module_(std::move(module))
{}

void DirectNativeDispatcherV5::setValueMemoryDescriptor(
    ScriptValueMemoryDescriptorFn descriptor, void* userData) noexcept
{
    if (module_.packageRuntime != nullptr) {
        module_.packageRuntime->configureValueMemoryDescriptor(descriptor, userData);
    } else if (module_.context != nullptr) {
        module_.context->valueMemoryDescriptor = descriptor;
        module_.context->valueMemoryDescriptorUserData = userData;
    }
}

bool DirectNativeDispatcherV5::hasFunction(const char* functionId) const
{
    if (module_.module == nullptr || !present(functionId))
        return false;
    for (std::size_t index = 0; index < module_.module->functionCount; ++index) {
        if (std::strcmp(module_.module->functions[index].functionId, functionId) == 0) {
            return true;
        }
    }
    return false;
}

Value DirectNativeDispatcherV5::invoke(
    const char* functionId, const Value* args, std::size_t argumentCount, VmError* error)
{
    VmError localError;
    VmError* activeError = error != nullptr ? error : &localError;
    activeError->code = ScriptDiagnosticCode::None;
    activeError->message.clear();
    activeError->traceback.clear();
    DirectNativeTelemetryEventV5 event;
    event.packageIdentity = module_.packageIdentity;
    event.moduleId = module_.moduleId;
    event.functionId = present(functionId) ? functionId : "<missing>";
    event.argumentCount = argumentCount;
    if (module_.module == nullptr || module_.context == nullptr || !present(functionId)) {
        activeError->code = ScriptDiagnosticCode::HybridPolicyRejected;
        activeError->message = "AotOnly v5 module binding is incomplete";
        event.diagnostic = activeError->code;
        telemetry_.push_back(std::move(event));
        return Value::undefined();
    }
    if (memoryLimitReached(*module_.context)) {
        memoryLimitExceeded(*module_.context, activeError);
        event.diagnostic = activeError->code;
        telemetry_.push_back(std::move(event));
        return makeNil();
    }
    if (argumentCount > module_.context->budget.maxCallArguments) {
        activeError->code = ScriptDiagnosticCode::ResourceLimitExceeded;
        activeError->message = "DirectNative call exceeds the argument limit";
        event.diagnostic = activeError->code;
        telemetry_.push_back(std::move(event));
        return makeNil();
    }
    if (argumentCount != 0u && args == nullptr) {
        activeError->code = ScriptDiagnosticCode::AotArtifactInvalid;
        activeError->message = "DirectNative call arguments are missing for a non-empty call";
        event.diagnostic = activeError->code;
        telemetry_.push_back(std::move(event));
        return makeNil();
    }
    const AotFunctionDescriptorV5* function = nullptr;
    for (std::size_t index = 0; index < module_.module->functionCount; ++index) {
        const auto& candidate = module_.module->functions[index];
        if (std::strcmp(candidate.functionId, functionId) == 0) {
            function = &candidate;
            break;
        }
    }
    event.budget = module_.module->budget;
    if (function == nullptr) {
        activeError->code = ScriptDiagnosticCode::AotMissingEntry;
        activeError->message = "AotOnly v5 DirectNative function is not registered";
        event.diagnostic = activeError->code;
        telemetry_.push_back(std::move(event));
        return Value::undefined();
    }
    event.functionIdentity = function->functionIdentity;
    if ((static_cast<std::uint32_t>(function->osrCapability)
            & static_cast<std::uint32_t>(AotOsrCapability::OperationBoundary))
        != 0u) {
        event.osrBoundary = "operation";
    }
    // Native engine methods can synchronously reenter this module (e.g.
    // add_component calls a script constructor). Such calls share the active
    // instruction and recursion budget rather than replenishing it.
    if (module_.context->recursionDepth == 0u) {
        if (module_.packageRuntime != nullptr)
            module_.packageRuntime->resetBudgets();
        else
            module_.context->resetBudget(module_.module->budget);
    }
    if (module_.context->memoryBudgetFailed) {
        memoryLimitExceeded(activeError);
        event.diagnostic = activeError->code;
        telemetry_.push_back(std::move(event));
        return makeNil();
    }
    if (!module_.context->moduleInitialized) {
        const char* initializerId = module_.module->moduleInitFunctionId;
        if (present(initializerId) && std::strcmp(initializerId, functionId) != 0) {
            const AotFunctionDescriptorV5* initializer = nullptr;
            for (std::size_t index = 0; index < module_.module->functionCount; ++index) {
                if (std::strcmp(module_.module->functions[index].functionId, initializerId) == 0) {
                    initializer = &module_.module->functions[index];
                    break;
                }
            }
            if (initializer == nullptr) {
                activeError->code = ScriptDiagnosticCode::AotMissingEntry;
                activeError->message = "AotOnly v5 module initializer is not registered";
                event.diagnostic = activeError->code;
                telemetry_.push_back(std::move(event));
                return Value::undefined();
            }
            DirectNativeTelemetryEventV5 initializerEvent;
            initializerEvent.packageIdentity = module_.packageIdentity;
            initializerEvent.moduleId = module_.moduleId;
            initializerEvent.functionId = initializer->functionId;
            initializerEvent.functionIdentity = initializer->functionIdentity;
            initializerEvent.argumentCount = 0u;
            initializerEvent.budget = module_.module->budget;
            if ((static_cast<std::uint32_t>(initializer->osrCapability)
                    & static_cast<std::uint32_t>(AotOsrCapability::OperationBoundary))
                != 0u) {
                initializerEvent.osrBoundary = "operation";
            }
            const auto recordInitializer = [&]() {
                initializerEvent.diagnostic = activeError->code;
                telemetry_.push_back(std::move(initializerEvent));
            };
            try {
                core::CpuMemoryQuotaScope quotaScope{module_.context->memoryQuota};
                MemoryRootOwnerScope initializerRoots{*module_.context};
                if (!initializerRoots.synchronize(nullptr, 0u, nullptr, activeError)) {
                    recordInitializer();
                    event.diagnostic = activeError->code;
                    telemetry_.push_back(std::move(event));
                    return makeNil();
                }
                (void)initializer->entry(*module_.context, nullptr, 0u, nullptr, activeError);
                if (activeError->code == ScriptDiagnosticCode::None
                    && !initializerRoots.synchronize(nullptr, 0u, nullptr, activeError)) {
                    recordInitializer();
                    event.diagnostic = activeError->code;
                    telemetry_.push_back(std::move(event));
                    return makeNil();
                }
            } catch (const std::bad_alloc&) {
                memoryLimitExceeded(*module_.context, activeError);
            }
            if (activeError->code != ScriptDiagnosticCode::None) {
                if (activeError->code == ScriptDiagnosticCode::ResourceLimitExceeded)
                    memoryLimitExceeded(*module_.context, activeError);
                recordInitializer();
                event.diagnostic = activeError->code;
                telemetry_.push_back(std::move(event));
                return Value::undefined();
            }
            recordInitializer();
            module_.context->moduleInitialized = true;
        } else if (!present(initializerId)) {
            module_.context->moduleInitialized = true;
        }
    }
    Value result;
    MemoryRootOwnerScope invocationRoots{*module_.context};
    try {
        core::CpuMemoryQuotaScope quotaScope{module_.context->memoryQuota};
        const ScriptValueSpan argumentRoots{args, argumentCount};
        if (!invocationRoots.synchronize(
                &argumentRoots, 1u, module_.context->activeCaptures, activeError)) {
            event.diagnostic = activeError->code;
            telemetry_.push_back(std::move(event));
            return makeNil();
        }
        result = function->entry(*module_.context, args, argumentCount, nullptr, activeError);
        const ScriptValueSpan resultRoot{&result, 1u};
        if ((activeError->code == ScriptDiagnosticCode::None
                || activeError->code == ScriptDiagnosticCode::Suspended)
            && !invocationRoots.synchronize(
                &resultRoot, 1u, module_.context->activeCaptures, activeError))
            result = makeNil();
    } catch (const std::bad_alloc&) {
        memoryLimitExceeded(*module_.context, activeError);
        result = makeNil();
    }
    if (activeError->code == ScriptDiagnosticCode::ResourceLimitExceeded
        || module_.context->memoryBudgetFailed) {
        memoryLimitExceeded(*module_.context, activeError);
        result = makeNil();
    }
    if (activeError->code == ScriptDiagnosticCode::None
        && present(module_.module->moduleInitFunctionId)
        && std::strcmp(module_.module->moduleInitFunctionId, functionId) == 0) {
        module_.context->moduleInitialized = true;
    }
    event.diagnostic = activeError->code;
    telemetry_.push_back(std::move(event));
    return result;
}

Value DirectNativeDispatcherV5::invokeMethod(const char* methodId,
    const Value& receiver,
    const Value* args,
    std::size_t argumentCount,
    VmError* error)
{
    VmError localError;
    VmError* activeError = error != nullptr ? error : &localError;
    activeError->code = ScriptDiagnosticCode::None;
    activeError->message.clear();
    activeError->traceback.clear();
    if (module_.context == nullptr || module_.module == nullptr || !present(methodId)) {
        activeError->code = ScriptDiagnosticCode::AotModuleUnavailable;
        activeError->message = "AOT instance method binding is incomplete";
        return makeNil();
    }
    if (!receiver.is<arp::reflection::instance>()
        || !receiver.ptr<arp::reflection::instance>()->alive()) {
        activeError->code = ScriptDiagnosticCode::BadCast;
        activeError->message = "AOT instance method receiver is not a script instance";
        return makeNil();
    }
    // Construct from the memory resource pointer explicitly.  Passing the
    // pointer keeps the allocator-only constructor selected; passing an
    // allocator object in an initializer list can be interpreted as a Value
    // element and silently adds a second argument before the receiver.
    core::pmr::Vector<Value> callArgs{&core::defaultCpuMemoryResource()};
    callArgs.reserve(argumentCount + 1u);
    callArgs.push_back(receiver);
    if (argumentCount > 0u && args != nullptr)
        callArgs.insert(callArgs.end(), args, args + argumentCount);
    const auto telemetryStart = telemetry_.size();
    auto result = invoke(methodId, callArgs.data(), callArgs.size(), activeError);
    // invoke() also records a module initializer when this is the first call;
    // annotate only the method event so the matrix distinguishes explicit
    // arguments from the prepended receiver.
    for (std::size_t index = telemetry_.size(); index > telemetryStart; --index) {
        auto& event = telemetry_[index - 1u];
        if (event.functionId == methodId) {
            event.explicitArgumentCount = argumentCount;
            event.receiverPrepended = true;
            break;
        }
    }
    return result;
}

} // namespace arp::scriptvm

#include "scriptvm/runtime/value.h"

#include <array>
#include <atomic>
#include <limits>

namespace arp::scriptvm {

namespace {
constexpr std::size_t kMaxGeneratorBridges = 4u;

int depthOverflow(int remaining) noexcept
{
    if (remaining < 0 || remaining == std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return remaining + 1;
}

std::array<std::atomic<const GeneratorBridgeV2*>, kMaxGeneratorBridges> gGeneratorBridges{};
std::array<std::atomic<const GeneratorBridgeV3*>, kMaxGeneratorBridges> gGeneratorBridgesV3{};
std::array<std::atomic<const GeneratorBridgeV3Capability*>, kMaxGeneratorBridges>
    gGeneratorBridgeV3Capabilities{};

const GeneratorBridgeV2* providerFor(const Value& value) noexcept
{
    for (const auto& slot : gGeneratorBridges) {
        const auto* provider = slot.load(std::memory_order_acquire);
        if (provider != nullptr && provider->isGenerator != nullptr && provider->isGenerator(value))
            return provider;
    }
    return nullptr;
}

const GeneratorBridgeV3* providerForV3(const Value& value) noexcept
{
    for (const auto& slot : gGeneratorBridgesV3) {
        const auto* provider = slot.load(std::memory_order_acquire);
        if (provider != nullptr && provider->contractVersion == kGeneratorBridgeV3ContractVersion
            && provider->structSize >= sizeof(GeneratorBridgeV3) && provider->isGenerator != nullptr
            && provider->isGenerator(value))
            return provider;
    }
    return nullptr;
}

const GeneratorBridgeV3Capability* providerForV3Capability(const Value& value) noexcept
{
    for (const auto& slot : gGeneratorBridgeV3Capabilities) {
        const auto* provider = slot.load(std::memory_order_acquire);
        if (provider != nullptr
            && provider->contractVersion == kGeneratorBridgeV3CapabilityContractVersion
            && provider->structSize >= sizeof(GeneratorBridgeV3Capability)
            && provider->isGenerator != nullptr && provider->isGenerator(value))
            return provider;
    }
    return nullptr;
}

bool compositeIsGenerator(const Value& value) noexcept
{
    return providerFor(value) != nullptr;
}

int compositeDelegatedAwaitDepth(const Value& value, int remaining) noexcept
{
    if (remaining <= 0)
        return depthOverflow(remaining);
    const auto* provider = providerFor(value);
    return provider != nullptr && provider->delegatedAwaitDepth != nullptr
        ? provider->delegatedAwaitDepth(value, remaining)
        : 0;
}

bool compositeIsDone(const Value& value) noexcept
{
    const auto* provider = providerFor(value);
    return provider == nullptr || provider->isDone == nullptr || provider->isDone(value);
}

bool compositeIsSuspended(const Value& value) noexcept
{
    const auto* provider = providerFor(value);
    return provider != nullptr && provider->isSuspended != nullptr && provider->isSuspended(value);
}

Value compositeResume(Value& value, VmError* error)
{
    const auto* provider = providerFor(value);
    return provider != nullptr && provider->resume != nullptr ? provider->resume(value, error)
                                                              : Value::undefined();
}

Value compositeResumeSend(Value& value, const Value& sent, VmError* error)
{
    const auto* provider = providerFor(value);
    return provider != nullptr && provider->resumeSend != nullptr
        ? provider->resumeSend(value, sent, error)
        : Value::undefined();
}

Value compositeResumeThrow(Value& value, const Value& thrown, VmError* error)
{
    const auto* provider = providerFor(value);
    return provider != nullptr && provider->resumeThrow != nullptr
        ? provider->resumeThrow(value, thrown, error)
        : Value::undefined();
}

void compositeClose(Value& value) noexcept
{
    const auto* provider = providerFor(value);
    if (provider != nullptr && provider->close != nullptr)
        provider->close(value);
}

void compositeRegisterReflection()
{
    for (const auto& slot : gGeneratorBridges) {
        const auto* provider = slot.load(std::memory_order_acquire);
        if (provider != nullptr && provider->registerReflection != nullptr)
            provider->registerReflection();
    }
}

bool compositeV3IsGenerator(const Value& value) noexcept
{
    return providerForV3(value) != nullptr;
}

int compositeV3DelegatedAwaitDepth(const Value& value, int remaining) noexcept
{
    if (remaining <= 0)
        return depthOverflow(remaining);
    const auto* provider = providerForV3(value);
    return provider != nullptr && provider->delegatedAwaitDepth != nullptr
        ? provider->delegatedAwaitDepth(value, remaining)
        : 0;
}

bool compositeV3IsDone(const Value& value) noexcept
{
    const auto* provider = providerForV3(value);
    return provider == nullptr || provider->isDone == nullptr || provider->isDone(value);
}

bool compositeV3IsSuspended(const Value& value) noexcept
{
    const auto* provider = providerForV3(value);
    return provider != nullptr && provider->isSuspended != nullptr && provider->isSuspended(value);
}

Value compositeV3Resume(Value& value, VmError* error)
{
    const auto* provider = providerForV3(value);
    return provider != nullptr && provider->resume != nullptr ? provider->resume(value, error)
                                                              : Value::undefined();
}

Value compositeV3ResumeSend(Value& value, const Value& sent, VmError* error)
{
    const auto* provider = providerForV3(value);
    return provider != nullptr && provider->resumeSend != nullptr
        ? provider->resumeSend(value, sent, error)
        : Value::undefined();
}

Value compositeV3ResumeThrow(Value& value, const Value& thrown, VmError* error)
{
    const auto* provider = providerForV3(value);
    return provider != nullptr && provider->resumeThrow != nullptr
        ? provider->resumeThrow(value, thrown, error)
        : Value::undefined();
}

void compositeV3Close(Value& value) noexcept
{
    const auto* provider = providerForV3(value);
    if (provider != nullptr && provider->close != nullptr)
        provider->close(value);
}

void compositeV3RegisterReflection()
{
    for (const auto& slot : gGeneratorBridgesV3) {
        const auto* provider = slot.load(std::memory_order_acquire);
        if (provider != nullptr && provider->registerReflection != nullptr)
            provider->registerReflection();
    }
}

bool compositeV3QueryIdentity(const Value& value, GeneratorIdentityV3& identity) noexcept
{
    const auto* provider = providerForV3(value);
    return provider != nullptr && provider->queryIdentity != nullptr
        && provider->queryIdentity(value, identity);
}

bool compositeV3SupportsPendingAwaitInjection(const Value& value) noexcept
{
    const auto* provider = providerForV3Capability(value);
    return provider != nullptr && provider->supportsPendingAwaitInjection != nullptr
        && provider->supportsPendingAwaitInjection(value);
}

const GeneratorBridgeV2 gCompositeBridge{
    &compositeIsGenerator,
    &compositeDelegatedAwaitDepth,
    &compositeIsDone,
    &compositeIsSuspended,
    &compositeResume,
    &compositeResumeSend,
    &compositeResumeThrow,
    &compositeClose,
    &compositeRegisterReflection,
};

const GeneratorBridgeV3 gCompositeBridgeV3{
    kGeneratorBridgeV3ContractVersion,
    sizeof(GeneratorBridgeV3),
    &compositeV3IsGenerator,
    &compositeV3DelegatedAwaitDepth,
    &compositeV3IsDone,
    &compositeV3IsSuspended,
    &compositeV3Resume,
    &compositeV3ResumeSend,
    &compositeV3ResumeThrow,
    &compositeV3Close,
    &compositeV3RegisterReflection,
    &compositeV3QueryIdentity,
};

const GeneratorBridgeV3Capability gCompositeBridgeV3Capability{
    kGeneratorBridgeV3CapabilityContractVersion,
    sizeof(GeneratorBridgeV3Capability),
    &compositeV3IsGenerator,
    &compositeV3SupportsPendingAwaitInjection,
};
} // namespace

const GeneratorBridgeV2* generatorBridgeV2() noexcept
{
    return &gCompositeBridge;
}

void installGeneratorBridgeV2(const GeneratorBridgeV2* bridge) noexcept
{
    if (bridge == nullptr)
        return;
    for (auto& slot : gGeneratorBridges) {
        const auto* current = slot.load(std::memory_order_acquire);
        if (current == bridge)
            return;
        if (current == nullptr) {
            const GeneratorBridgeV2* expected = nullptr;
            if (slot.compare_exchange_strong(
                    expected, bridge, std::memory_order_release, std::memory_order_relaxed))
                return;
        }
    }
}

const GeneratorBridgeV3* generatorBridgeV3() noexcept
{
    return &gCompositeBridgeV3;
}

void installGeneratorBridgeV3(const GeneratorBridgeV3* bridge) noexcept
{
    if (bridge == nullptr || bridge->contractVersion != kGeneratorBridgeV3ContractVersion
        || bridge->structSize < sizeof(GeneratorBridgeV3) || bridge->isGenerator == nullptr
        || bridge->queryIdentity == nullptr)
        return;
    for (auto& slot : gGeneratorBridgesV3) {
        const auto* current = slot.load(std::memory_order_acquire);
        if (current == bridge)
            return;
        if (current == nullptr) {
            const GeneratorBridgeV3* expected = nullptr;
            if (slot.compare_exchange_strong(
                    expected, bridge, std::memory_order_release, std::memory_order_relaxed))
                return;
        }
    }
}

const GeneratorBridgeV3Capability* generatorBridgeV3Capability() noexcept
{
    return &gCompositeBridgeV3Capability;
}

void installGeneratorBridgeV3Capability(const GeneratorBridgeV3Capability* capability) noexcept
{
    if (capability == nullptr
        || capability->contractVersion != kGeneratorBridgeV3CapabilityContractVersion
        || capability->structSize < sizeof(GeneratorBridgeV3Capability)
        || capability->isGenerator == nullptr
        || capability->supportsPendingAwaitInjection == nullptr)
        return;
    for (auto& slot : gGeneratorBridgeV3Capabilities) {
        const auto* current = slot.load(std::memory_order_acquire);
        if (current == capability)
            return;
        if (current == nullptr) {
            const GeneratorBridgeV3Capability* expected = nullptr;
            if (slot.compare_exchange_strong(
                    expected, capability, std::memory_order_release, std::memory_order_relaxed))
                return;
        }
    }
}

} // namespace arp::scriptvm

#pragma once

// ArpScript: value model (thick-bridge).
//
// In ArpScript every runtime value is a reflection::object. This file provides
// thin helpers around object for the VM's needs: constructing scalars, reading
// scalars by C++ type, and the ScriptCell type used for closure captures.
//
// ScriptClosure, ScriptCell, and ScriptMethod are plain C++ structs registered into the
// reflection db so they can be carried inside an object (the VM never holds
// raw pointers to them -- it always goes through object::ptr<>()).
//
// All-reference semantics note: object is SharedPtr<variant>, so assigning a
// value to a slot copies the shared pointer (aliasing). For immutable scalars
// (int/float/bool/string) this is indistinguishable from value copy. For
// mutable containers (list/dict/instance) mutations are visible through all
// aliases -- this is the intended Python-like reference semantics.

#include "core/callback.h"
#include "core/cpu_allocator.h"
#include "reflection/reflection.h"

#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace arp::scriptvm {

using Value = arp::reflection::object;

// A narrow proof of actual owned payload storage, without conversion, native
// pointer resolution or reflection registration. Selector misses retain the
// caller's existing general Value semantics, including module/cv boundaries.
template <class T> const T* exactValuePayload(const Value& value) noexcept
{
    using SharedVariant = arp::reflection::detail::ObjectRef<arp::reflection::variant>;
    return value.cast<const SharedVariant&>()->template exactPayloadPointer<T>();
}

// Actual typed access with the original general pointer semantics on a selector
// miss. Unlike a conversion accessor, this never invokes magic cast functions.
// Mutable access matches Value::ptr<T>()'s existing const-carrier API.
template <class T> T* checkedValuePayload(const Value& value)
{
    if (const auto* actual = exactValuePayload<T>(value))
        return const_cast<T*>(actual);
    return value.is<T>() ? value.ptr<T>() : nullptr;
}

// A successful exact selector proves owned storage, including exclusion of
// NativeObjectReference. Selector misses retain the original explicit native
// rejection and general cv/module pointer access. This does not prove unique
// ownership: mutation callers must establish that independently.
template <class T> T* checkedOwnedValuePayload(const Value& value)
{
    if (const auto* actual = exactValuePayload<T>(value))
        return const_cast<T*>(actual);
    if (value.is_native_reference())
        return nullptr;
    return value.is<T>() ? value.ptr<T>() : nullptr;
}

struct VmError;
struct Program;
class Vm;
// Opaque same-process hint owned by an optional native generator bridge.
// Runtime values intentionally do not expose AOT descriptor types.
using ScriptAotModuleHandle = const void*;
using ScriptClosureBridgeFn = Value (*)(
    void*, const core::pmr::String&, const core::pmr::Vector<Value>&, VmError*);

// Optional host bridge providers for native generator carriers. Providers are
// composed by the runtime without exposing their native generator types to the
// VM value ABI. A provider may be absent when the minimal AOT-only runtime is
// linked.
struct GeneratorBridgeV2 {
    bool (*isGenerator)(const Value&) noexcept = nullptr;
    // Return the number of pending delegated generator edges reachable from
    // `value`. `remaining` is the caller's bounded traversal budget; a
    // provider must return a value greater than `remaining` when that budget
    // is exhausted or the state is malformed. This keeps cyclic/unbounded
    // delegation fail-closed without imposing a global depth constant.
    int (*delegatedAwaitDepth)(const Value&, int) noexcept = nullptr;
    bool (*isDone)(const Value&) noexcept = nullptr;
    bool (*isSuspended)(const Value&) noexcept = nullptr;
    Value (*resume)(Value&, VmError*) = nullptr;
    Value (*resumeSend)(Value&, const Value&, VmError*) = nullptr;
    Value (*resumeThrow)(Value&, const Value&, VmError*) = nullptr;
    void (*close)(Value&) noexcept = nullptr;
    void (*registerReflection)() = nullptr;
};

// Versioned identity contract for cross-backend generator delegation. This is
// deliberately separate from GeneratorBridgeV2 so adding identity negotiation
// cannot make an older provider's shorter struct unsafe to read.
inline constexpr std::uint32_t kGeneratorBridgeV3ContractVersion = 1u;

struct GeneratorIdentityV3 {
    const char* moduleId = nullptr;
    std::uint32_t moduleVersion = 0u;
    const char* artifactIdentity = nullptr;

    bool complete() const noexcept
    {
        return moduleId != nullptr && moduleId[0] != '\0' && moduleVersion != 0u
            && artifactIdentity != nullptr && artifactIdentity[0] != '\0';
    }
};

using GeneratorIdentityQueryV3 = bool (*)(const Value&, GeneratorIdentityV3&) noexcept;

struct GeneratorBridgeV3 {
    std::uint32_t contractVersion = kGeneratorBridgeV3ContractVersion;
    std::uint32_t structSize = sizeof(GeneratorBridgeV3);
    bool (*isGenerator)(const Value&) noexcept = nullptr;
    // Same bounded-depth contract as GeneratorBridgeV2.
    int (*delegatedAwaitDepth)(const Value&, int) noexcept = nullptr;
    bool (*isDone)(const Value&) noexcept = nullptr;
    bool (*isSuspended)(const Value&) noexcept = nullptr;
    Value (*resume)(Value&, VmError*) = nullptr;
    Value (*resumeSend)(Value&, const Value&, VmError*) = nullptr;
    Value (*resumeThrow)(Value&, const Value&, VmError*) = nullptr;
    void (*close)(Value&) noexcept = nullptr;
    void (*registerReflection)() = nullptr;
    GeneratorIdentityQueryV3 queryIdentity = nullptr;
};

// Optional capability extension for the stable V3 identity bridge. This is a
// separate contract so adding a capability cannot change the ABI or version
// of GeneratorBridgeV3. Providers must return false when the value is not
// their carrier or when the pending await has no explicit injection contract.
inline constexpr std::uint32_t kGeneratorBridgeV3CapabilityContractVersion = 1u;
using GeneratorPendingAwaitInjectionQueryV3 = bool (*)(const Value&) noexcept;

struct GeneratorBridgeV3Capability {
    std::uint32_t contractVersion = kGeneratorBridgeV3CapabilityContractVersion;
    std::uint32_t structSize = sizeof(GeneratorBridgeV3Capability);
    bool (*isGenerator)(const Value&) noexcept = nullptr;
    GeneratorPendingAwaitInjectionQueryV3 supportsPendingAwaitInjection = nullptr;
};

ARP_API const GeneratorBridgeV2* generatorBridgeV2() noexcept;
ARP_API void installGeneratorBridgeV2(const GeneratorBridgeV2* bridge) noexcept;
ARP_API const GeneratorBridgeV3* generatorBridgeV3() noexcept;
ARP_API void installGeneratorBridgeV3(const GeneratorBridgeV3* bridge) noexcept;
ARP_API const GeneratorBridgeV3Capability* generatorBridgeV3Capability() noexcept;
ARP_API void installGeneratorBridgeV3Capability(
    const GeneratorBridgeV3Capability* capability) noexcept;

// Immutable byte sequence carried as a first-class ArpScript value. The
// backing storage uses the engine PMR allocator and is exposed read-only to
// script code; mutation must happen by constructing a new value.
struct ScriptBytes {
    core::pmr::Vector<std::uint8_t> data{&core::defaultCpuMemoryResource()};

    ScriptBytes() = default;

    explicit ScriptBytes(core::pmr::Vector<std::uint8_t> bytes) : data(std::move(bytes)) {}
};

// These exact owned C++ payloads cannot also be either generator carrier.
// This proof does not classify native/cv/module selector misses and does not
// suppress a host provider predicate: those callbacks remain observable.
inline bool hasExactOwnedCollectionPayload(const Value& value) noexcept
{
    return exactValuePayload<arp::reflection::hlist>(value) != nullptr
        || exactValuePayload<arp::reflection::hdict>(value) != nullptr
        || exactValuePayload<ScriptBytes>(value) != nullptr;
}

enum class ScriptAwaitStatus : std::uint8_t {
    Pending,
    Ready,
    Failed,
};

enum class ScriptAwaitInjectionKind : std::uint8_t {
    Send,
    Throw,
};

// Pending-await injection is an explicit host contract. A host may reject an
// injection, keep the await pending, complete it with a value, or report a
// terminal failure. There is no implicit cancellation or replay fallback.
enum class ScriptAwaitInjectionStatus : std::uint8_t {
    Rejected,
    Pending,
    Ready,
    Failed,
};

// A host-owned asynchronous result. The VM only observes it on the script
// thread; completion producers must publish their state before the host
// calls Vm::resume().
struct ScriptAwaitable {
    using PollFn = core::SharedCallback<ScriptAwaitStatus(Value&, core::pmr::String&)>;
    using ReadyFn = core::SharedCallback<bool()>;
    using InjectFn = core::SharedCallback<ScriptAwaitInjectionStatus(
        ScriptAwaitInjectionKind, const Value&, Value&, core::pmr::String&)>;

    PollFn poll;
    ReadyFn ready;
    InjectFn inject;

    // Optional engine-owned operation associated with this await adapter.
    // The value is the canonical native reflection object, never a script
    // handle or proxy. Generic awaitables may leave it undefined.
    Value operation;

    ScriptAwaitStatus pollResult(Value& value, core::pmr::String& diagnostics) const
    {
        return poll ? poll(value, diagnostics) : ScriptAwaitStatus::Failed;
    }

    ScriptAwaitInjectionStatus injectResult(ScriptAwaitInjectionKind kind,
        const Value& value,
        Value& result,
        core::pmr::String& diagnostics) const
    {
        return inject ? inject(kind, value, result, diagnostics)
                      : ScriptAwaitInjectionStatus::Rejected;
    }

    bool supportsInjection() const noexcept { return static_cast<bool>(inject); }

    bool isReady() const { return ready ? ready() : false; }

    bool hasOperation() const { return !operation.is_undefined(); }

    Value operationObject() const { return operation; }
};

// --- scalar helpers ---

// makeInt uses a bounded lazy cache for the non-negative uint16 range. The
// cache is implemented in value.cpp as thread-local state so values never
// cross VM threads through shared cache ownership; its allocations stay on
// the core CPU allocator path and only values actually used by a VM are
// materialized.
inline bool intValueUsesCache(int v) noexcept
{
    return static_cast<unsigned>(v) <= std::numeric_limits<std::uint16_t>::max();
}

ARP_API Value makeInt(int v);

// A same-thread view of the real cache entry array. Keep it within a synchronous
// C++ execution scope on that thread; never store it in a continuation. The array
// remains stable across synchronous host reentry, including cold publication.
// It owns nothing and never snapshots payloads or entry pointers.
// Bind only after semantic admission; cold misses keep makeInt's normal path.
class IntegerValueCacheBorrow final {
public:
    IntegerValueCacheBorrow() = default;
    ARP_API static IntegerValueCacheBorrow current() noexcept;

    bool valid() const noexcept { return entries_ != nullptr; }

    // A guard must never materialize a cold carrier. Entries published later
    // in the same admitted interval are visible through this actual array.
    const Value* find(int value) const noexcept
    {
        if (!entries_ || !intValueUsesCache(value))
            return nullptr;
#ifndef NDEBUG
        assert(entries_ == current().entries_);
#endif
        return entries_[static_cast<std::size_t>(value)];
    }

    Value makeCached(int value) const
    {
        assert(entries_ && intValueUsesCache(value));
#ifndef NDEBUG
        assert(entries_ == current().entries_);
#endif
        if (const auto* cached = entries_[static_cast<std::size_t>(value)])
            return *cached;
        return makeInt(value);
    }

private:
    explicit IntegerValueCacheBorrow(Value* const* entries) : entries_(entries) {}

    Value* const* entries_ = nullptr;
};

// Append to storage reserved by the semantic caller after its count/limit
// admission. The resulting elements use makeInt's exact carriers and quota
// policy. No detached integers, source snapshots or additional cache are made.
class IntegerValueBuilder final {
public:
    ARP_API static void appendReservedRange(
        arp::reflection::hlist& destination, int start, int end);
};

// Nonallocating lookup of the exact thread-owned mutable cache carrier.
ARP_API const Value* findCachedIntValue(int v) noexcept;

inline Value makeInt64(std::int64_t v)
{
    return Value{v};
}

inline Value makeFloat(double v)
{
    return Value{v};
}

ARP_API Value makeBool(bool v);

inline Value makeNil()
{
    return Value::undefined();
}

ARP_API Value makeString(core::pmr::String s);
ARP_API Value makeString(std::string_view s);
ARP_API Value makeBytes(core::pmr::Vector<std::uint8_t> bytes);
ARP_API Value makeBytes(std::string_view s);

// Read accessors use reflection::object::cast<T>(), so registered __int__,
// __float__, __bool__, and __str__ magic functions are honored. Return false
// when the conversion contract is unavailable or fails.
ARP_API bool tryGetInt(const Value& v, int& out);
ARP_API bool tryGetInt64(const Value& v, std::int64_t& out);
ARP_API bool tryGetFloat(const Value& v, double& out);
ARP_API bool tryGetBool(const Value& v, bool& out);
ARP_API bool tryGetString(const Value& v, core::pmr::String& out);
ARP_API bool tryGetBytes(const Value& v, core::pmr::Vector<std::uint8_t>& out);
ARP_API const ScriptBytes* asScriptBytes(const Value& v);

// Returns true if v is nil (undefined object or null).
ARP_API bool isNil(const Value& v);

// A mutable cell that can be captured by closures. Registered into the
// reflection db so it can be stored inside an object (VM slots/globals).
struct ScriptCell {
    Value value;

    explicit ScriptCell(Value v = {}) : value(std::move(v)) {}
};

// A module version keeps its immutable Program/AOT context alive while
// script values from that version are still reachable. The dispatcher checks
// this counter before retiring a version; the default limit is unbounded.
struct ScriptModuleRetentionBudget {
    std::size_t maxRetainedValues = std::numeric_limits<std::size_t>::max();
    std::size_t retainedValues = 0;
    bool retired = false;
};

struct ScriptModuleRetentionLease {
    std::shared_ptr<ScriptModuleRetentionBudget> budget;

    explicit ScriptModuleRetentionLease(std::shared_ptr<ScriptModuleRetentionBudget> value)
        : budget(std::move(value))
    {
        if (budget != nullptr)
            ++budget->retainedValues;
    }

    ~ScriptModuleRetentionLease()
    {
        if (budget != nullptr && budget->retainedValues > 0u)
            --budget->retainedValues;
    }
};

inline std::shared_ptr<ScriptModuleRetentionLease> makeScriptModuleRetentionLease(
    const std::shared_ptr<ScriptModuleRetentionBudget>& budget)
{
    return budget == nullptr ? nullptr : core::makeShared<ScriptModuleRetentionLease>(budget);
}

// A closure: function index + captured cells. Registered into reflection.
// Owner metadata is part of the runtime value so hot reload cannot silently
// reinterpret an old function index against a new module.
struct ScriptClosure {
    int funcIndex = 0;
    arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> captures{
        &arp::core::defaultCpuMemoryResource()};
    std::shared_ptr<ScriptModuleRetentionLease> retentionLease;
    std::shared_ptr<const Program> programOwner;
    std::shared_ptr<Vm> vmOwner;
    ScriptAotModuleHandle aotModule = nullptr;
    std::shared_ptr<void> aotContextOwner;
    std::shared_ptr<void> moduleOwner;
    std::uint32_t moduleVersion = 0;
    core::pmr::String functionId{&core::defaultCpuMemoryResource()};
    ScriptClosureBridgeFn bridge = nullptr;
    void* bridgeUserData = nullptr;

    ScriptClosure() = default;

    ScriptClosure(int idx, arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> caps)
        : funcIndex(idx), captures(std::move(caps))
    {}
};

enum class ScriptPointerKind : std::uint8_t {
    Cell,
    Index,
    Field,
};

// A pointer to a script storage location, used by &-addressing and
// StoreIndirect/Deref. Cell remains the first field for source compatibility
// with existing aggregate construction in generated artifacts.
struct ScriptPointer {
    std::shared_ptr<ScriptCell> cell;
    ScriptPointerKind kind = ScriptPointerKind::Cell;
    Value base;
    Value index;
    core::pmr::String field{&core::defaultCpuMemoryResource()};

    explicit ScriptPointer(std::shared_ptr<ScriptCell> c = {}) : cell(std::move(c)) {}

    ScriptPointer(Value container, Value key)
        : kind(ScriptPointerKind::Index), base(std::move(container)), index(std::move(key))
    {}

    ScriptPointer(Value object, core::pmr::String fieldName)
        : kind(ScriptPointerKind::Field), base(std::move(object)), field(std::move(fieldName))
    {}
};

// A temporary method value bound to one script instance. The receiver is
// stored as a reflection object so field mutations and subsequent method calls
// share the same instance storage. Script instances do not retain these values;
// the VM creates them when a method field is loaded.
struct ScriptMethod {
    int funcIndex = 0;
    Value receiver;

    ScriptMethod() = default;

    ScriptMethod(int idx, Value object) : funcIndex(idx), receiver(std::move(object)) {}
};

struct ScriptGenerator;

// A temporary C++ reflection method bound to a host instance. Like
// ScriptMethod, it must not be stored back into the receiver.
struct ReflectedMethod {
    arp::reflection::function function;
    Value receiver;

    ReflectedMethod(arp::reflection::function reflected, Value object)
        : function(std::move(reflected)), receiver(std::move(object))
    {}
};

// Register VM and reflection bridge carriers into the reflection db.
// Idempotent; safe to call multiple times.
void registerScriptObjectReflection();

ARP_API bool isScriptAwaitable(const Value& value);
ARP_API const ScriptAwaitable* asScriptAwaitable(const Value& value);
ARP_API bool isScriptGenerator(const Value& value);
ARP_API ScriptGenerator* asScriptGenerator(const Value& value);

} // namespace arp::scriptvm

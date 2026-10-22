#pragma once

#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/compile/ast.h"
#include "scriptvm/plugin/plugin_services.h"
#include "scriptvm/runtime/host.h"
#include "scriptvm/runtime/module_registration.h"
#include "scriptvm/runtime/value.h"

#include <functional>
#include <memory>
#include <utility>

namespace arp::scriptvm {

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

} // namespace arp::scriptvm

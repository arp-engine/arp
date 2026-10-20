#pragma once

// ArpScript: Host bridge (Phase 6).
//
// HostRegistry stores C++ functions registered for script invocation.
// The VM's CallHost opcode looks up a function by index and invokes it
// with arguments from the stack, pushing the return value back.
//
// Host functions are stored as reflection::function, so they can accept
// and return reflection::object (the VM's Value type) natively.

#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/runtime/value.h"

#include <cstddef>
#include <cstdint>
#include <functional>

namespace arp::scriptvm {

using HostFn = std::function<Value(const arp::core::pmr::Vector<Value>&)>;

// Authoritative metadata for a host callable that may later participate in a
// Native CallValue contract. An empty contract is deliberately not inferred
// from a registry index or a runtime Value.
struct HostCallableContract {
    core::pmr::String bindingId{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String signatureHash{&arp::core::defaultCpuMemoryResource()};
    int fixedArity = -1;
    bool maySuspend = false;
    std::uint32_t lifetimeVersion = 0;

    bool valid() const
    {
        return !bindingId.empty() && !signatureHash.empty() && fixedArity >= 0
            && lifetimeVersion != 0;
    }

    // Explicit metadata makes arity part of the callable ABI. An empty
    // contract remains the legacy dynamically-typed host path.
    bool acceptsArity(std::size_t argumentCount) const
    {
        return !valid() || fixedArity == static_cast<int>(argumentCount);
    }
};

// Explicit runtime identity and artifact metadata for a reflection callable.
// The identity is an opaque reflection::function wrapper token; it is only
// compared in-process and must never be serialized into an AOT artifact.
struct ReflectionCallableContract {
    core::pmr::String bindingId{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String targetId{&arp::core::defaultCpuMemoryResource()};
    core::pmr::String signatureHash{&arp::core::defaultCpuMemoryResource()};
    int fixedArity = -1;
    bool maySuspend = false;
    std::uint32_t lifetimeVersion = 0;
    const void* functionIdentity = nullptr;

    bool valid() const
    {
        return !bindingId.empty() && !targetId.empty() && !signatureHash.empty() && fixedArity >= 0
            && lifetimeVersion != 0u && functionIdentity != nullptr;
    }
};

class HostRegistry {
public:
    HostRegistry() : funcs_(&arp::core::defaultCpuMemoryResource()) {}

    // Register a host function. Returns its index for CallHost.
    int registerFunction(const core::pmr::String& name, HostFn fn)
    {
        int idx = static_cast<int>(funcs_.size());
        HostEntry entry;
        entry.name = name;
        entry.fn = std::move(fn);
        funcs_.push_back(std::move(entry));
        nameIndex_[name] = idx;
        return idx;
    }

    // Metadata-bearing registration is explicit. Invalid metadata is rejected
    // so callers cannot accidentally create a partially trusted Native binding.
    int registerFunction(const core::pmr::String& name, HostFn fn, HostCallableContract contract)
    {
        if (!contract.valid())
            return -1;
        int idx = static_cast<int>(funcs_.size());
        HostEntry entry;
        entry.name = name;
        entry.fn = std::move(fn);
        entry.contract = std::move(contract);
        funcs_.push_back(std::move(entry));
        nameIndex_[name] = idx;
        return idx;
    }

    // Look up a host function by index.
    const HostFn* lookup(int index) const
    {
        if (index < 0 || index >= static_cast<int>(funcs_.size()))
            return nullptr;
        return &funcs_[index].fn;
    }

    const HostCallableContract* lookupContract(int index) const
    {
        if (index < 0 || index >= static_cast<int>(funcs_.size()))
            return nullptr;
        const auto& contract = funcs_[index].contract;
        return contract.valid() ? &contract : nullptr;
    }

    // Look up a host function by name. Returns -1 if not found.
    int lookupIndex(const core::pmr::String& name) const
    {
        auto it = nameIndex_.find(name);
        if (it == nameIndex_.end())
            return -1;
        return it->second;
    }

    int lookupBindingIndex(const core::pmr::String& bindingId) const
    {
        for (std::size_t index = 0; index < funcs_.size(); ++index) {
            if (funcs_[index].contract.valid() && funcs_[index].contract.bindingId == bindingId)
                return static_cast<int>(index);
        }
        return -1;
    }

    int count() const { return static_cast<int>(funcs_.size()); }

private:
    struct HostEntry {
        core::pmr::String name{&arp::core::defaultCpuMemoryResource()};
        HostFn fn;
        HostCallableContract contract;
    };

    arp::core::pmr::Vector<HostEntry> funcs_;
    core::pmr::Map<core::pmr::String, int> nameIndex_{&arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm

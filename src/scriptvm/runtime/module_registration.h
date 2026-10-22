#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/reflection.h"
#include "scriptvm/runtime/host.h"

namespace arp::scriptvm {

// Records reflected types exported by one logical script module. Reflection
// owns class_info; ScriptVM owns the module-level export boundary.
struct ModuleTypeBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    const arp::reflection::class_info* classInfo = nullptr;
};

struct ModuleFunctionBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    arp::reflection::function function;
};

struct ModuleEnumBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    arp::reflection::enumeration enumeration;
};

struct ModuleObjectBinding {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    arp::reflection::object value;
};

// Codegen-owned callable metadata. The reflected function remains owned by
// its class_info/module registration; this record only makes its stable ABI
// visible to the ScriptVM compiler and AOT package builder.
struct ModuleReflectionCallableBinding {
    core::pmr::String key{&core::defaultCpuMemoryResource()};
    ReflectionCallableContract contract;
};

class ARP_API ModuleRegistration final {
public:
    explicit ModuleRegistration(arp::reflection::NameView name);

    bool bindType(arp::reflection::NameView name, const arp::reflection::class_info* classInfo);
    bool bindEnum(arp::reflection::NameView name, arp::reflection::enumeration enumeration);
    bool exportFunction(arp::reflection::NameView name, arp::reflection::function function);
    bool exportFunction(arp::reflection::NameView name,
        arp::reflection::function function,
        ReflectionCallableContract contract);
    bool registerReflectionCallable(
        arp::reflection::NameView key, ReflectionCallableContract contract);
    bool bindObject(arp::reflection::NameView name, arp::reflection::object value);
    const core::pmr::String& name() const;
    const core::pmr::Vector<ModuleTypeBinding>& types() const;
    const core::pmr::Vector<ModuleFunctionBinding>& functions() const;
    const core::pmr::Vector<ModuleReflectionCallableBinding>& reflectionCallables() const;
    const core::pmr::Vector<ModuleEnumBinding>& enums() const;
    const core::pmr::Vector<ModuleObjectBinding>& objects() const;
    void remapReflectionTypes(const arp::reflection::RegistrationTransaction& transaction);

private:
    core::pmr::String name_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ModuleTypeBinding> types_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ModuleFunctionBinding> functions_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ModuleReflectionCallableBinding> reflectionCallables_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<ModuleEnumBinding> enums_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ModuleObjectBinding> objects_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm

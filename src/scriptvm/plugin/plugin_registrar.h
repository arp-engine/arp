#pragma once

#include "scriptvm/runtime/module_exports.h"

namespace arp::scriptvm {

// Registrar for one C++ plugin module. The registrar owns the mutable export
// table during plugin entry and freezes it when build() is called. No plugin
// receives ModuleRegistry or an implementation-specific host-module builder.
class ARP_API ScriptPluginRegistrar final {
public:
    explicit ScriptPluginRegistrar(
        core::pmr::String name, ScriptPluginServiceRegistry* services = nullptr)
        : name_(std::move(name)), exports_(arp::core::makeShared<ModuleExports>()),
          reflectionModule_(arp::core::makeShared<ModuleRegistration>(name_)), services_(services)
    {
        exports_->reflectionRegistration = reflectionModule_;
    }

    // Export an arbitrary Value (function, object, scalar).
    bool exportValue(core::pmr::String name, Value v);

    // Retain native engine objects created during plugin registration for the
    // lifetime of the immutable export table. This is the only ownership
    // bridge for module-bound native objects; scripts receive reflection
    // references, never the owner itself.
    bool retainNativeLifetime(std::shared_ptr<void> lifetime);

    // Bind any existing C++ reflection type without creating a script-side
    // type mirror. Construction is available when the class_info has __new__;
    // static reflected fields and enum descriptors remain available otherwise.
    bool bindType(core::pmr::String name, const arp::reflection::class_info* classInfo);

    // Bind a codegen-produced enumeration as a module-level symbol. The
    // enumeration remains reflection metadata; script member access resolves
    // its named values through the VM's existing enumeration path.
    bool bindEnum(core::pmr::String name, arp::reflection::enumeration enumeration)
    {
        if (frozen_ || name.empty() || exports_->symbols.find(name) != exports_->symbols.end()) {
            return false;
        }
        return reflectionModule_->bindEnum(name, std::move(enumeration));
    }

    ModuleRegistration* reflectionModule() { return reflectionModule_.get(); }

    template <typename T> bool bindType(core::pmr::String name)
    {
        return bindType(std::move(name), arp::reflection::class_of<T>());
    }

    // Compatibility spelling for callers that specifically bind a class.
    bool bindClass(core::pmr::String name, const arp::reflection::class_info* classInfo);

    template <typename T> bool bindClass(core::pmr::String name)
    {
        return bindType<T>(std::move(name));
    }

    // Export a host function (wrapped as a Value via ScriptClosure-like
    // carrier; the VM calls it through CallValue).
    // For simplicity we store the HostFn in a HostCallable struct registered
    // into reflection, and the VM's CallValue dispatches it.
    bool exportCallable(core::pmr::String name, HostFn fn);

    // A metadata-bearing export is the only HostCallable authoring path that
    // can later qualify for a Native indirect-call contract.
    bool exportCallable(core::pmr::String name, HostFn fn, HostCallableContract contract);

    // Service-backed callables are the only hand-authored plugin exports.
    // They capture a host service instance and therefore cannot be emitted by
    // reflection codegen. Keeping a distinct spelling makes this exception
    // auditable in plugin entry points and enforceable by the build contract.
    bool exportServiceCallable(core::pmr::String name, HostFn fn)
    {
        return exportCallable(std::move(name), std::move(fn));
    }

    bool exportServiceCallable(core::pmr::String name, HostFn fn, HostCallableContract contract)
    {
        return exportCallable(std::move(name), std::move(fn), std::move(contract));
    }

    // Explicit alias retained for source plugins whose exported operation is
    // a dynamically typed callable. Both spellings use the same validation
    // and export-table path; there is no separate host-module API.
    bool exportFunction(core::pmr::String name, HostFn fn)
    {
        return exportCallable(std::move(name), std::move(fn));
    }

    bool exportFunction(core::pmr::String name, HostFn fn, HostCallableContract contract)
    {
        return exportCallable(std::move(name), std::move(fn), std::move(contract));
    }

    // Export a typed reflection function. Its parameter and return contracts
    // remain available to sema while the VM invokes the same reflected value.
    bool exportFunction(core::pmr::String name, arp::reflection::function fn);
    bool exportFunction(
        core::pmr::String name, arp::reflection::function fn, ReflectionCallableContract contract);

    bool exportServiceFunction(core::pmr::String name, arp::reflection::function fn)
    {
        return exportFunction(std::move(name), std::move(fn));
    }

    // Finalize and return the immutable export table.
    std::shared_ptr<ModuleExports> build();

    // Counts domain exports authored by the plugin entry. The two ABI
    // metadata symbols inserted by build() are deliberately excluded.
    std::size_t explicitDomainExportCount() const noexcept
    {
        return explicitExportCount_ + reflectionModule_->types().size()
            + reflectionModule_->functions().size() + reflectionModule_->enums().size()
            + reflectionModule_->objects().size();
    }

    const core::pmr::String& name() const { return name_; }

    ScriptPluginServiceRegistry* services() const { return services_; }

private:
    core::pmr::String name_;
    std::shared_ptr<ModuleExports> exports_;
    std::shared_ptr<ModuleRegistration> reflectionModule_;
    ScriptPluginServiceRegistry* services_ = nullptr;
    std::shared_ptr<void> nativeLifetime_;
    std::size_t explicitExportCount_ = 0u;
    bool frozen_ = false;
};

} // namespace arp::scriptvm

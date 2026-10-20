#pragma once
#include "core/sha256.h"
#include "scriptvm/interpreter/pipeline.h"
#include "scriptvm/interpreter/vm.h"

namespace arp::scriptvm {
// Per-world stateful execution. Uses the canonical module/value ABI.
class ScriptSession final {
public:
    bool bindNative(const AotPackageDescriptorV5& package,
        const char* module,
        ModuleRegistry& registry,
        const core::pmr::String& source)
    {
        registry_ = &registry;
        nativeRegistry_.setPluginDependencyResolver(
            &resolveAotPluginDependencyFromModuleRegistry, &registry);
        auto registered = nativeRegistry_.registerPackage(package);
        if (!registered.ok()) {
            diagnostic_ = registered.diagnostic().message;
            return false;
        }
        ScriptAotModuleInstanceV5 instance;
        auto created = createAotOnlyModuleInstanceV5(
            nativeRegistry_, package.packageIdentity, module, instance);
        if (!created.ok()) {
            diagnostic_ = created.diagnostic().message;
            return false;
        }
        if (instance.module->sourceHash != core::Sha256::hexDigest(core::StringView(source))) {
            diagnostic_ = "ARP-SCRIPT-AOT-SOURCE-MISMATCH";
            return false;
        }
        auto configured = configureAotOnlyModuleExportV5(
            instance,
            [](void* p, const char* m, const char* name, Value* result, VmError* error) {
                auto* value = static_cast<ModuleRegistry*>(p)->findExport(m, name);
                if (!value) {
                    error->code = ScriptDiagnosticCode::AotModuleUnavailable;
                    error->message = "ARP-SCRIPT-MODULE-EXPORT-MISSING";
                    return false;
                }
                *result = *value;
                return true;
            },
            &registry);
        if (!configured.ok()) {
            diagnostic_ = configured.diagnostic().message;
            return false;
        }
        for (std::uint32_t i = 0; i < instance.module->pluginDependencyCount; ++i) {
            auto exports = registry.loadModule(instance.module->pluginDependencies[i].moduleId);
            if (!exports) {
                diagnostic_ = "ARP-SCRIPT-SERVICE-MODULE-MISSING";
                return false;
            }
            for (const auto& e : exports->reflectionCallableContracts) {
                std::optional<reflection::function> function;
                const reflection::class_info* owner = nullptr;
                auto value = exports->symbols.find(e.first);
                const auto modulePrefix =
                    core::pmr::String(instance.module->pluginDependencies[i].moduleId) + ".";
                if (value == exports->symbols.end()
                    && e.first.compare(0, modulePrefix.size(), modulePrefix) == 0)
                    value = exports->symbols.find(e.first.substr(modulePrefix.size()));
                if (value != exports->symbols.end() && value->second.is<reflection::function>())
                    function = value->second.cast<reflection::function>();
                else if (exports->reflectionRegistration)
                    for (const auto& type : exports->reflectionRegistration->types()) {
                        const auto prefix =
                            core::pmr::String(instance.module->pluginDependencies[i].moduleId) + "."
                            + type.name + ".";
                        if (e.first.compare(0, prefix.size(), prefix) != 0)
                            continue;
                        const auto member =
                            type.classInfo->get_field(e.first.substr(prefix.size()));
                        if (member.is<reflection::function>()) {
                            function = member.cast<reflection::function>();
                            owner = type.classInfo;
                            break;
                        }
                    }
                if (!function || function->identity() != e.second.functionIdentity) {
                    diagnostic_ = "ARP-SCRIPT-SERVICE-CONTRACT";
                    return false;
                }
                if (!bound_
                        .emplace(core::StringView(e.second.bindingId),
                            BoundFunction{*function,
                                e.second,
                                instance.module->pluginDependencies[i].moduleId,
                                owner})
                        .second) {
                    diagnostic_ = "ARP-SCRIPT-BINDING-DUPLICATE";
                    return false;
                }
            }
        }
        budget_ = instance.module->budget;
        nativeArguments_.reserve(budget_.maxCallArguments);
        auto bound = configureAotOnlyBoundCallV5(
            instance,
            [](void* p,
                const char* binding,
                const char* target,
                const Value* receiver,
                const Value* args,
                std::size_t count,
                Value* result,
                VmError* error) {
                auto& self = *static_cast<ScriptSession*>(p);
                auto found = self.bound_.find(core::StringView(binding));
                const char* reason = nullptr;
                if (found == self.bound_.end())
                    reason = "missing-binding";
                else if (found->second.contract.targetId != target)
                    reason = "target";
                else if (found->second.contract.fixedArity != int(count)
                    || count > self.budget_.maxCallArguments)
                    reason = "arity";
                else if (receiver) {
                    // DirectNative preserves an imported module operand for statically
                    // proven calls; dynamic reflective calls carry the function itself.
                    if (receiver->is<DirectNativeModuleReferenceV5>()) {
                        if (receiver->ptr<DirectNativeModuleReferenceV5>()->moduleId
                            != found->second.moduleId)
                            reason = "module-identity";
                    } else if (found->second.owner) {
                        if (receiver->is_native_reference() && !receiver->native_reference_alive())
                            reason = "stale-receiver";
                        else if (!receiver->class_()
                            || (!found->second.owner->is_base_of(receiver->class_())
                                && found->second.owner != receiver->class_()))
                            reason = "receiver-type";
                    } else if (!receiver->is<reflection::function>()
                        || receiver->cast<reflection::function>().identity()
                            != found->second.function.identity())
                        reason = "callable-identity";
                }
                if (reason) {
                    error->code = ScriptDiagnosticCode::AotModuleUnavailable;
                    error->message = core::pmr::String("ARP-SCRIPT-BOUND-CALL-CONTRACT: ") + binding
                        + " / " + reason;
                    return false;
                }
                // A reflected method can synchronously attach another script component.
                // Each call owns its argument view so reentry cannot overwrite it.
                core::pmr::Vector<Value> callArguments;
                if (count)
                    callArguments.assign(args, args + count);
                try {
                    *result = found->second.function.call_v(
                        found->second.owner && receiver ? *receiver : makeNil(), callArguments);
                    return true;
                } catch (const std::exception& e) {
                    error->code = ScriptDiagnosticCode::HostError;
                    error->message = e.what();
                    return false;
                }
            },
            this);
        if (!bound.ok()) {
            diagnostic_ = bound.diagnostic().message;
            return false;
        }
        nativeModule_ = core::pmr::String("arp-fn:") + module + "/";
        native_ = core::makeUnique<DirectNativeDispatcherV5>(std::move(instance));
        native_->setValueMemoryDescriptor(
            [](void* context, const Value& value, ScriptValueMemoryDescriptor& out) noexcept {
                // This exact carrier borrows process-owned reflection metadata.
                // Its core carrier bytes are already charged by the accountant.
                const auto* type = exactValuePayload<ReflectedTypeBinding>(value);
                if (!type || !type->classInfo) {
                    auto& self = *static_cast<ScriptSession*>(context);
                    return self.descriptor_
                        && self.descriptor_(self.descriptorContext_, value, out);
                }
                out.allocationIdentity = type->classInfo;
                out.retainedBytes = 0;
                return true;
            },
            this);
        return true;
    }

    bool build(const core::pmr::String& source,
        const core::pmr::String& module,
        ModuleRegistry& registry,
        const core::pmr::Vector<core::pmr::String>& dependencies,
        AotExecutionBudgetV5 budget,
        const char* entry = "create")
    {
        ScriptInterpreterBuildRequest request;
        request.source = source;
        request.filename = module + ".arp";
        request.moduleId = module;
        request.moduleRegistry = &registry;
        if (entry && *entry)
            request.entryFunctionId = "arp-fn:" + module + "/" + entry;
        if (!budget.instructionLimit || !budget.recursionLimit || !budget.maxContainerValues
            || !budget.maxCallArguments) {
            diagnostic_ = "ARP-SCRIPT-EXECUTION-BUDGET";
            return false;
        }
        budget_ = budget;
        core::pmr::String contract, graph;
        for (const auto& name : dependencies) {
            auto exports = registry.loadModule(name);
            if (!exports) {
                diagnostic_ = "ARP-SCRIPT-DEPENDENCY-MISSING";
                return false;
            }
            char version[32];
            std::snprintf(version, sizeof(version), "%u", exports->moduleVersion);
            graph += name + ":" + version + ";";
            contract += exports->exportContractHash + exports->reflectionRegistryHash;
        }
        request.dependencyGraphHash = core::Sha256::hexDigest(core::StringView(graph));
        request.reflectionRegistryHash = core::Sha256::hexDigest(core::StringView(contract));
        built_ = ScriptInterpreterPipeline::build(request);
        registry_ = &registry;
        vm_.setModuleRegistry(&registry);
        if (!built_.ok()) {
            diagnostic_ = built_.error.message;
            char location[64];
            std::snprintf(
                location, sizeof(location), " (%d:%d)", built_.error.line, built_.error.column);
            diagnostic_ += location;
            return false;
        }
        vm_.setInstructionBudget(budget_.instructionLimit);
        vm_.setRecursionLimit(budget_.recursionLimit);
        vm_.setMaxContainerValues(budget_.maxContainerValues);
        vm_.setMaxCallArguments(budget_.maxCallArguments);
        vm_.setMaxFrameValues(budget_.maxFrameValues);
        vm_.setMemoryQuota(core::makeShared<core::CpuMemoryQuota>(budget_.maxMemoryBytes));
        auto validated = ValidatedInterpreterProgram::create(built_.program, program_);
        if (!validated.ok()) {
            diagnostic_ = validated.diagnostic().message;
            return false;
        }
        vm_.bindProgram(program_);
        return true;
    }

    bool call(const char* function, const core::pmr::Vector<Value>& arguments, Value& result)
    {
        vm_.setInstructionBudget(budget_.instructionLimit);
        VmError error;
        if (native_) {
            native_->clearTelemetry();
            result = native_->invoke(
                (nativeModule_ + function).c_str(), arguments.data(), arguments.size(), &error);
        } else
            result = vm_.runFunctionByName(
                program_.program(), core::pmr::String(function), arguments, &error);
        if (error.code != ScriptDiagnosticCode::None) {
            diagnostic_ =
                core::pmr::String(function) + ": " + error.message + "\n" + error.traceback;
            return false;
        }
        return true;
    }

    void setValueMemoryDescriptor(ScriptValueMemoryDescriptorFn descriptor, void* context)
    {
        descriptor_ = descriptor;
        descriptorContext_ = context;
    }

    void retainModules(core::SharedPtr<ModuleRegistry> modules)
    {
        registryOwner_ = std::move(modules);
    }

    const auto& diagnostic() const { return diagnostic_; }

    const auto& typedIr() const { return built_.typedIr; }

    struct Entry {
        core::pmr::String id;
        int index = -1;
    };

    bool resolve(const core::pmr::String& id, Entry& entry) const
    {
        entry.id = id;
        if (native_)
            return native_->hasFunction(id.c_str());
        const auto found = program_.program().functionIdIndex.find(id);
        if (found == program_.program().functionIdIndex.end())
            return false;
        entry.index = found->second;
        return true;
    }

    bool invoke(const Entry& entry, const core::pmr::Vector<Value>& arguments, Value& result)
    {
        vm_.setInstructionBudget(budget_.instructionLimit);
        VmError error;
        if (native_) {
            native_->clearTelemetry();
            result = native_->invoke(entry.id.c_str(), arguments.data(), arguments.size(), &error);
        } else
            result = vm_.runFunction(program_.program(), entry.index, arguments, &error);
        if (error.code != ScriptDiagnosticCode::None) {
            diagnostic_ = entry.id + ": " + error.message + "\n" + error.traceback;
            return false;
        }
        return true;
    }

private:
    ScriptValueMemoryDescriptorFn descriptor_ = nullptr;
    void* descriptorContext_ = nullptr;
    AotExecutionBudgetV5 budget_;
    ScriptInterpreterBuildResult built_;
    Vm vm_;
    ModuleRegistry* registry_ = nullptr;
    core::pmr::String diagnostic_;
    ValidatedInterpreterProgram program_;
    core::SharedPtr<ModuleRegistry> registryOwner_;
    ScriptAotPackageRegistry nativeRegistry_;
    core::UniquePtr<DirectNativeDispatcherV5> native_;
    core::pmr::String nativeModule_;

    struct BoundFunction {
        reflection::function function;
        ReflectionCallableContract contract;
        core::pmr::String moduleId;
        const reflection::class_info* owner = nullptr;
    };

    core::pmr::Map<core::StringView, BoundFunction> bound_;
    core::pmr::Vector<Value> nativeArguments_;
};
} // namespace arp::scriptvm

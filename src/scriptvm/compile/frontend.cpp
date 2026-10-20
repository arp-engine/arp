#include "scriptvm/compile/frontend.h"

#include "scriptvm/compile/sema.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace arp::scriptvm {

namespace {

using CallableDictBindings = core::pmr::Vector<std::pair<core::pmr::String, core::pmr::String>>;

struct DirectLocal {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    int slot = -1;
    bool parameter = false;
    bool captured = false;
    TypePtr type;
    // Non-empty only when every statically known write to this value carries
    // the same imported callable contract. Any overwrite clears it.
    core::pmr::String callableBindingId{&core::defaultCpuMemoryResource()};
    // A bounded container fact for a local list whose every element is a
    // statically proven callable. Dynamic indices and unknown elements do not
    // populate this vector.
    std::vector<core::pmr::String> callableListBindings;
    CallableDictBindings callableDictBindings{&core::defaultCpuMemoryResource()};
};

struct DirectScope {
    DirectScope* parent = nullptr;
    std::vector<std::unique_ptr<DirectLocal>> locals;

    DirectLocal* findLocal(const core::pmr::String& name) const
    {
        for (const auto& local : locals) {
            if (local->name == name)
                return local.get();
        }
        return nullptr;
    }

    DirectLocal* find(const core::pmr::String& name) const
    {
        if (auto* local = findLocal(name))
            return local;
        return parent != nullptr ? parent->find(name) : nullptr;
    }
};

struct DirectCapture {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    int slot = -1;
    DirectLocal* source = nullptr;
    core::pmr::String callableBindingId{&core::defaultCpuMemoryResource()};
    std::vector<core::pmr::String> callableListBindings;
    CallableDictBindings callableDictBindings{&core::defaultCpuMemoryResource()};
};

struct DirectFunction {
    TypedIrFunction ir;
    core::pmr::Vector<TypedIrOperationRecord> operations{&core::defaultCpuMemoryResource()};
    std::vector<std::unique_ptr<DirectScope>> scopes;
    DirectScope* scope = nullptr;
    std::vector<DirectCapture> captures;
    TypePtr returnType;
    int nextSlot = 0;
    int currentStack = 0;
    int maxStack = 0;
    bool moduleInit = false;
    std::vector<int> breakJumps;
    std::vector<int> continueJumps;

    void pushScope()
    {
        auto next = std::make_unique<DirectScope>();
        next->parent = scope;
        scope = next.get();
        scopes.push_back(std::move(next));
    }

    void popScope()
    {
        if (scope != nullptr)
            scope = scope->parent;
    }

    int alloc(const core::pmr::String& name, bool parameter, TypePtr type)
    {
        if (auto* existing = scope->findLocal(name)) {
            if (existing->type == nullptr && type != nullptr)
                existing->type = std::move(type);
            return existing->slot;
        }
        return allocFresh(name, parameter, std::move(type));
    }

    int allocTemporary(TypePtr type = {})
    {
        core::pmr::String name("__direct_tmp_", &core::defaultCpuMemoryResource());
        name += std::to_string(nextSlot);
        return allocFresh(name, false, std::move(type));
    }

private:
    int allocFresh(const core::pmr::String& name, bool parameter, TypePtr type)
    {
        auto local = std::make_unique<DirectLocal>();
        local->name = name;
        local->slot = nextSlot++;
        local->parameter = parameter;
        local->type = std::move(type);
        const int slot = local->slot;
        scope->locals.push_back(std::move(local));
        return slot;
    }

public:
    void push(int count = 1)
    {
        currentStack += count;
        maxStack = std::max(maxStack, currentStack);
    }

    void pop(int count = 1) { currentStack = std::max(0, currentStack - count); }
};

TypedIrTypeRef directType(const TypePtr& type)
{
    TypedIrTypeRef result;
    if (!type)
        return result;
    if (type->kind == TypeKind::Optional) {
        result = directType(type->elem);
        result.nullable = true;
        return result;
    }
    switch (type->kind) {
    case TypeKind::List:
        result.kind = TypedIrType::List;
        break;
    case TypeKind::Dict:
        result.kind = TypedIrType::Dict;
        break;
    case TypeKind::Func:
        result.kind = TypedIrType::Function;
        break;
    case TypeKind::Class:
        result.kind = TypedIrType::Class;
        result.className =
            type->reflectedClass != nullptr ? type->reflectedClass->name() : type->name;
        break;
    case TypeKind::Basic:
        if (type->name == "int")
            result.kind = TypedIrType::Int;
        else if (type->name == "int64")
            result.kind = TypedIrType::Int64;
        else if (type->name == "float")
            result.kind = TypedIrType::Float;
        else if (type->name == "bool")
            result.kind = TypedIrType::Bool;
        else if (type->name == "string")
            result.kind = TypedIrType::String;
        else if (type->name == "bytes")
            result.kind = TypedIrType::Bytes;
        else
            result.kind = TypedIrType::Any;
        break;
    case TypeKind::Optional:
        break;
    }
    return result;
}

bool directIntType(const TypePtr& type)
{
    return type && type->kind == TypeKind::Basic && type->name == "int";
}

bool directInt64Type(const TypePtr& type)
{
    return type && type->kind == TypeKind::Basic && type->name == "int64";
}

bool directFloatType(const TypePtr& type)
{
    return type && type->kind == TypeKind::Basic && type->name == "float";
}

bool directBoolType(const TypePtr& type)
{
    return type && type->kind == TypeKind::Basic && type->name == "bool";
}

TypedIrOperation directOperationKind(TypedIrOpcode opcode)
{
    return typedIrOperationDescriptor(opcode).operation;
}

std::uint32_t directEffects(TypedIrOpcode opcode)
{
    return typedIrOperationDescriptor(opcode).requiredEffects;
}

TypedIrCallKind directCallKind(TypedIrOpcode opcode)
{
    return typedIrOperationDescriptor(opcode).callKind;
}

bool directTerminator(TypedIrOpcode opcode)
{
    return typedIrOperationDescriptor(opcode).terminator;
}

bool directConditional(TypedIrOpcode opcode)
{
    return typedIrOperationDescriptor(opcode).conditionalBranch;
}

bool sameCallableBinding(const TypedIrCallableBinding& lhs, const TypedIrCallableBinding& rhs)
{
    return lhs.bindingId == rhs.bindingId && lhs.origin == rhs.origin
        && lhs.targetId == rhs.targetId && lhs.signatureHash == rhs.signatureHash
        && lhs.fixedArity == rhs.fixedArity && lhs.maySuspend == rhs.maySuspend
        && lhs.generatorBridge == rhs.generatorBridge && lhs.lifetimeVersion == rhs.lifetimeVersion;
}

bool appendCallableBinding(
    core::pmr::Vector<TypedIrCallableBinding>& bindings, const TypedIrCallableBinding& binding)
{
    const auto existing = std::find_if(
        bindings.begin(), bindings.end(), [&binding](const TypedIrCallableBinding& candidate) {
            return candidate.bindingId == binding.bindingId;
        });
    if (existing == bindings.end()) {
        bindings.push_back(binding);
        return true;
    }
    return sameCallableBinding(*existing, binding);
}

void collectDirectIds(const ExprPtr& expr, std::vector<core::pmr::String>& names);

void collectDirectStmtIds(const StmtPtr& stmt, std::vector<core::pmr::String>& names)
{
    if (!stmt)
        return;
    auto collectBody = [&names](const core::pmr::Vector<StmtPtr>& body) {
        for (const auto& nested : body)
            collectDirectStmtIds(nested, names);
    };
    switch (stmt->kind) {
    case StmtKind::VarDecl:
        collectDirectIds(stmt->expr, names);
        break;
    case StmtKind::Assign:
        collectDirectIds(stmt->target, names);
        collectDirectIds(stmt->value, names);
        break;
    case StmtKind::Expr:
    case StmtKind::Return:
    case StmtKind::Raise:
    case StmtKind::Yield:
        collectDirectIds(stmt->expr, names);
        break;
    case StmtKind::If:
        collectDirectIds(stmt->cond, names);
        collectBody(stmt->body);
        for (const auto& cond : stmt->elifConds)
            collectDirectIds(cond, names);
        for (const auto& body : stmt->elifBodies)
            for (const auto& nested : body)
                collectDirectStmtIds(nested, names);
        collectBody(stmt->elseBody);
        break;
    case StmtKind::While:
        collectDirectIds(stmt->cond, names);
        collectBody(stmt->body);
        break;
    case StmtKind::For:
        collectDirectIds(stmt->iterable, names);
        collectBody(stmt->body);
        break;
    case StmtKind::Try:
        collectBody(stmt->tryBody);
        for (const auto& clause : stmt->exceptClauses)
            for (const auto& nested : clause.body)
                collectDirectStmtIds(nested, names);
        break;
    default:
        break;
    }
}

void collectDirectIds(const ExprPtr& expr, std::vector<core::pmr::String>& names)
{
    if (!expr)
        return;
    if (expr->kind == ExprKind::Ident) {
        names.push_back(expr->name);
        return;
    }
    collectDirectIds(expr->left, names);
    collectDirectIds(expr->right, names);
    for (const auto& arg : expr->args)
        collectDirectIds(arg, names);
    for (const auto& key : expr->dictKeys)
        collectDirectIds(key, names);
    for (const auto& value : expr->dictValues)
        collectDirectIds(value, names);
}

class DirectModuleLowerer final {
public:
    explicit DirectModuleLowerer(const ScriptTypedModule& module)
        : moduleSnapshot_(module), module_(module.syntax), moduleId_(module.moduleId)
    {}

    TypedIrBuildResult run()
    {
        collectDeclarations();
        if (!ok())
            return failure();
        for (const auto& decl : module_->decls) {
            if (decl->kind == StmtKind::FuncDecl)
                compileFunction(decl, functionIndex_[decl->funcName.c_str()], {});
        }
        if (!ok())
            return failure();
        for (const auto& decl : module_->decls) {
            if (decl->kind != StmtKind::ClassDecl)
                continue;
            for (const auto& method : decl->methods) {
                const auto qualified = decl->className + "." + method->funcName;
                compileFunction(method, functionIndex_[qualified.c_str()], {});
            }
        }
        if (!ok())
            return failure();
        compileModuleInit();
        if (!ok())
            return failure();
        TypedIrBuildResult result;
        result.module.moduleId = moduleId_;
        result.module.globalCount = globalCount_;
        for (const auto& [name, slot] : globals_) {
            TypedIrGlobalSlot global;
            global.name = name;
            global.slot = slot;
            result.module.globals.push_back(std::move(global));
        }
        std::sort(result.module.globals.begin(),
            result.module.globals.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.slot < rhs.slot;
            });
        for (std::size_t index = 0; index < functions_.size(); ++index) {
            if (functions_[index]->moduleInit) {
                result.module.moduleInitFunctionId = functions_[index]->ir.id;
                break;
            }
        }
        for (const auto& binding : moduleSnapshot_.callableBindings) {
            if (binding.targetId.size() < 2u
                || binding.targetId.compare(binding.targetId.size() - 2u, 2u, ".*") != 0)
                if (!appendCallableBinding(result.module.callableBindings, binding)) {
                    setError(ScriptDiagnosticCode::TypedIrInvalid,
                        "typed IR callable binding contract conflicts",
                        {});
                    return failure();
                }
        }
        for (const auto& binding : synthesizedCallableBindings_) {
            if (!appendCallableBinding(result.module.callableBindings, binding)) {
                setError(ScriptDiagnosticCode::TypedIrInvalid,
                    "typed IR synthesized callable binding contract conflicts",
                    {});
                return failure();
            }
        }
        for (const auto& function : functions_) {
            finalize(*function);
            if (!ok())
                return failure();
        }
        for (const auto& dependency : dependencies_)
            result.module.dependencies.push_back(dependency);
        for (const auto& reflection : reflectionDependencies_)
            result.module.reflectionDependencies.push_back(reflection);
        for (const auto& host : hostCapabilities_)
            result.module.hostCapabilities.push_back(host);
        for (const auto& declaration : module_->decls) {
            if (declaration->kind != StmtKind::ClassDecl)
                continue;
            const auto fields = classFields_.find(declaration->className.c_str());
            if (fields == classFields_.end()) {
                setError(ScriptDiagnosticCode::TypedIrInvalid,
                    "script class schema is unavailable during Typed IR lowering",
                    declaration->pos);
                return failure();
            }
            TypedIrScriptClassSchema schema;
            schema.className = declaration->className;
            core::pmr::Set<core::pmr::String> visiting{&core::defaultCpuMemoryResource()};
            const auto collectMethods =
                [&](auto&& self,
                    const core::pmr::String& className,
                    core::pmr::Map<core::pmr::String, core::pmr::String>& methods) -> void {
                if (!ok())
                    return;
                if (!visiting.emplace(className).second) {
                    setError(ScriptDiagnosticCode::TypeMismatch,
                        core::pmr::String(
                            "cyclic script class inheritance: ", &core::defaultCpuMemoryResource())
                            + className,
                        SourcePos(module_->filename, 1, 1));
                    return;
                }
                for (const auto& candidate : module_->decls) {
                    if (candidate->kind != StmtKind::ClassDecl || candidate->className != className)
                        continue;
                    if (!candidate->baseClassName.empty())
                        self(self, candidate->baseClassName, methods);
                    for (const auto& method : candidate->methods) {
                        const auto qualified = candidate->className + "." + method->funcName;
                        const auto methodIndex = functionIndex_.find(qualified.c_str());
                        if (methodIndex != functionIndex_.end() && methodIndex->second >= 0
                            && methodIndex->second < static_cast<int>(functions_.size())) {
                            methods[method->funcName] =
                                functions_[static_cast<std::size_t>(methodIndex->second)]->ir.id;
                        }
                    }
                    return;
                }
            };
            collectMethods(collectMethods, declaration->className, schema.methods);
            if (!ok())
                return failure();
            for (const auto& field : fields->second)
                schema.fields.push_back(
                    core::pmr::String(field, &core::defaultCpuMemoryResource()));
            const auto constructor = constructors_.find(declaration->className.c_str());
            if (constructor != constructors_.end() && constructor->second >= 0
                && constructor->second < static_cast<int>(functions_.size())) {
                schema.constructorFunctionId =
                    functions_[static_cast<std::size_t>(constructor->second)]->ir.id;
            }
            result.module.scriptClassSchemas.push_back(std::move(schema));
        }
        for (const auto& function : functions_) {
            result.module.functions.push_back(function->ir);
        }
        annotateModuleInitClosureCallProvenance(result.module);
        result.module.fingerprint = computeTypedIrFingerprint(result.module);
        const auto validation = validateTypedIr(result.module);
        if (!validation.ok()) {
            result.error = validation.diagnostic();
            return result;
        }
        return result;
    }

private:
    const ScriptTypedModule& moduleSnapshot_;
    std::shared_ptr<Module> module_;
    core::pmr::String moduleId_{&core::defaultCpuMemoryResource()};
    ScriptDiagnostic error_;
    std::vector<std::unique_ptr<DirectFunction>> functions_;
    std::unordered_map<std::string, int> functionIndex_;
    std::unordered_map<std::string, std::vector<std::string>> classFields_;
    std::unordered_map<std::string, int> globals_;
    std::unordered_map<std::string, core::pmr::String> globalCallableBindings_;
    std::unordered_map<std::string, core::pmr::String> qualifiedCallableBindings_;
    std::unordered_map<std::string, core::pmr::String> importedModuleAliases_;
    std::unordered_map<std::string, core::pmr::String> externalModuleContractHashes_;
    core::pmr::Vector<TypedIrCallableBinding> synthesizedCallableBindings_{
        &core::defaultCpuMemoryResource()};
    std::unordered_map<std::string, core::pmr::String> globalBindingProvenance_;
    core::pmr::Map<core::pmr::String, core::pmr::Vector<core::pmr::String>>
        globalCallableListBindings_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::pair<core::pmr::String, CallableDictBindings>>
        globalCallableDictBindings_{&core::defaultCpuMemoryResource()};
    std::unordered_map<std::string, int> constructors_;
    int globalCount_ = 0;
    std::vector<core::pmr::String> dependencies_;
    std::vector<core::pmr::String> reflectionDependencies_;
    std::vector<core::pmr::String> hostCapabilities_;

    bool ok() const { return error_.code == ScriptDiagnosticCode::None; }

    TypedIrBuildResult failure() const
    {
        TypedIrBuildResult result;
        result.error = error_;
        return result;
    }

    void setError(ScriptDiagnosticCode code, const char* message, const SourcePos& pos)
    {
        if (!ok())
            return;
        error_ = ScriptDiagnostic(code,
            core::pmr::String(message, &core::defaultCpuMemoryResource()),
            core::pmr::String(pos.file, &core::defaultCpuMemoryResource()),
            pos.line,
            pos.column);
    }

    void setError(ScriptDiagnosticCode code, core::pmr::String message, const SourcePos& pos)
    {
        if (!ok())
            return;
        error_ = ScriptDiagnostic(code,
            std::move(message),
            core::pmr::String(pos.file, &core::defaultCpuMemoryResource()),
            pos.line,
            pos.column);
    }

    int addFunction(const core::pmr::String& name, const StmtPtr& decl)
    {
        auto function = std::make_unique<DirectFunction>();
        function->ir.name = name;
        function->ir.id = makeTypedIrFunctionId(moduleId_, name);
        function->ir.parameterCount = static_cast<int>(decl->paramNames.size());
        function->ir.signature.fixedParameterCount = decl->variadicParam
            ? std::max(0, decl->variadicParamIndex)
            : static_cast<int>(decl->paramNames.size());
        function->ir.signature.variadic = decl->variadicParam;
        function->returnType = decl->retType;
        function->ir.signature.result = directType(decl->retType);
        const int fixed = function->ir.signature.fixedParameterCount;
        for (int index = 0; index < fixed && index < static_cast<int>(decl->paramTypes.size());
            ++index)
            function->ir.signature.parameters.push_back(
                directType(decl->paramTypes[static_cast<std::size_t>(index)]));
        if (decl->variadicParam && decl->variadicParamIndex >= 0
            && decl->variadicParamIndex < static_cast<int>(decl->paramTypes.size())) {
            auto type = decl->paramTypes[static_cast<std::size_t>(decl->variadicParamIndex)];
            function->ir.signature.variadicElement =
                type && type->kind == TypeKind::List ? directType(type->elem) : directType(type);
        }
        const int index = static_cast<int>(functions_.size());
        functionIndex_[name.c_str()] = index;
        functions_.push_back(std::move(function));
        return index;
    }

    void collectDeclarations()
    {
        for (const auto& contract : moduleSnapshot_.callableBindings) {
            if (contract.targetId.size() >= 2u
                && contract.targetId.compare(contract.targetId.size() - 2u, 2u, ".*") == 0) {
                externalModuleContractHashes_[std::string(contract.targetId.data(),
                    contract.targetId.size() - 2u)] = contract.signatureHash;
            }
        }
        for (const auto& decl : module_->decls) {
            if (decl->kind == StmtKind::FuncDecl)
                addFunction(decl->funcName, decl);
        }
        for (const auto& decl : module_->decls) {
            if (decl->kind != StmtKind::ClassDecl)
                continue;
            std::vector<std::string> fields;
            if (!decl->baseClassName.empty()) {
                const auto base = classFields_.find(decl->baseClassName.c_str());
                if (base != classFields_.end())
                    fields = base->second;
            }
            for (const auto& field : decl->fields)
                fields.push_back(field.name.c_str());
            classFields_[decl->className.c_str()] = std::move(fields);
            for (const auto& method : decl->methods) {
                const core::pmr::String name = decl->className + "." + method->funcName;
                const int index = addFunction(name, method);
                if (method->isConstructor)
                    constructors_[decl->className.c_str()] = index;
            }
        }
        // Resolve inherited schemas after all class names are known. This
        // keeps class field indices independent of declaration order.
        std::function<void(const core::pmr::String&,
            std::vector<std::string>&,
            core::pmr::Set<core::pmr::String>&)>
            appendFields;
        appendFields = [this, &appendFields](const core::pmr::String& name,
                           std::vector<std::string>& fields,
                           core::pmr::Set<core::pmr::String>& visiting) {
            if (!ok())
                return;
            if (!visiting.emplace(name).second) {
                setError(ScriptDiagnosticCode::TypeMismatch,
                    core::pmr::String(
                        "cyclic script class inheritance: ", &core::defaultCpuMemoryResource())
                        + name,
                    SourcePos(module_->filename, 1, 1));
                return;
            }
            for (const auto& decl : module_->decls) {
                if (decl->kind != StmtKind::ClassDecl || decl->className != name)
                    continue;
                if (!decl->baseClassName.empty())
                    appendFields(decl->baseClassName, fields, visiting);
                for (const auto& field : decl->fields)
                    fields.push_back(field.name.c_str());
                return;
            }
        };
        for (const auto& decl : module_->decls) {
            if (decl->kind != StmtKind::ClassDecl)
                continue;
            std::vector<std::string> fields;
            core::pmr::Set<core::pmr::String> visiting{&core::defaultCpuMemoryResource()};
            appendFields(decl->className, fields, visiting);
            if (!ok())
                return;
            classFields_[decl->className.c_str()] = std::move(fields);
        }
        // A script subclass without its own __init__ inherits the nearest
        // base constructor. MakeInstance already supplies the concrete
        // receiver, so reusing the base function preserves the existing
        // constructor ABI while ensuring inherited fields are initialized.
        std::function<int(const std::string&, std::unordered_set<std::string>&)>
            resolveInheritedConstructor;
        resolveInheritedConstructor = [this, &resolveInheritedConstructor](const std::string& name,
                                          std::unordered_set<std::string>& visiting) -> int {
            const auto existing = constructors_.find(name);
            if (existing != constructors_.end())
                return existing->second;
            if (!visiting.emplace(name).second) {
                setError(ScriptDiagnosticCode::TypeMismatch,
                    core::pmr::String(
                        "cyclic script class inheritance: ", &core::defaultCpuMemoryResource())
                        + name.c_str(),
                    SourcePos(module_->filename, 1, 1));
                return -1;
            }
            const Stmt* declaration = nullptr;
            for (const auto& candidate : module_->decls) {
                if (candidate->kind == StmtKind::ClassDecl
                    && candidate->className == name.c_str()) {
                    declaration = candidate.get();
                    break;
                }
            }
            int inherited = -1;
            if (declaration != nullptr && !declaration->baseClassName.empty())
                inherited = resolveInheritedConstructor(
                    std::string(declaration->baseClassName.c_str()), visiting);
            visiting.erase(name);
            if (inherited >= 0)
                constructors_[name] = inherited;
            return inherited;
        };
        for (const auto& decl : module_->decls) {
            if (!ok() || decl->kind != StmtKind::ClassDecl)
                continue;
            if (constructors_.find(decl->className.c_str()) != constructors_.end())
                continue;
            std::unordered_set<std::string> visiting;
            resolveInheritedConstructor(std::string(decl->className.c_str()), visiting);
        }
        if (!ok())
            return;
        for (const auto& decl : module_->decls) {
            if (decl->kind == StmtKind::VarDecl)
                ensureGlobal(decl->varName);
            if (decl->kind != StmtKind::ImportDecl)
                continue;
            const auto matchesImportedCallable = [](const TypedIrCallableBinding& contract,
                                                     const core::pmr::String& moduleName,
                                                     const core::pmr::String& exportName) {
                if (contract.origin == TypedIrCallableOrigin::ScriptFunction) {
                    core::pmr::String prefix{"arp-fn:", &core::defaultCpuMemoryResource()};
                    prefix += moduleName;
                    prefix += "/";
                    return contract.targetId == prefix + exportName;
                }
                return contract.targetId == moduleName + "." + exportName;
            };
            if (decl->importIsModuleImport) {
                const auto& alias =
                    decl->importAlias.empty() ? decl->importModule : decl->importAlias;
                ensureGlobal(alias);
                importedModuleAliases_[alias.c_str()] = decl->importModule;
                for (const auto& contract : moduleSnapshot_.callableBindings) {
                    const auto separator = contract.targetId.rfind('/');
                    const auto dot = contract.targetId.rfind('.');
                    const bool scriptTarget =
                        contract.origin == TypedIrCallableOrigin::ScriptFunction
                        && contract.targetId.rfind("arp-fn:", 0) == 0
                        && separator != core::pmr::String::npos;
                    const auto exportName = scriptTarget
                        ? contract.targetId.substr(separator + 1u)
                        : (dot == core::pmr::String::npos ? core::pmr::String{}
                                                          : contract.targetId.substr(dot + 1u));
                    if (!exportName.empty()
                        && matchesImportedCallable(contract, decl->importModule, exportName))
                        qualifiedCallableBindings_[(alias + "." + exportName).c_str()] =
                            contract.bindingId;
                }
            } else {
                for (const auto& item : decl->importNames) {
                    const auto& binding = item.alias.empty() ? item.name : item.alias;
                    ensureGlobal(binding);
                    for (const auto& contract : moduleSnapshot_.callableBindings) {
                        if (matchesImportedCallable(contract, decl->importModule, item.name)) {
                            globalCallableBindings_[binding.c_str()] = contract.bindingId;
                            globalBindingProvenance_[binding.c_str()] = contract.bindingId;
                            break;
                        }
                    }
                }
            }
        }
        // Functions are lowered before module initialization is emitted. Seed
        // provenance for top-level aliases here so callers can validate the
        // final global cell contract without relying on initializer order.
        for (const auto& decl : module_->decls) {
            if (decl->kind == StmtKind::VarDecl) {
                setGlobalBinding(decl->varName, globalBindingForExpr(decl->expr));
                std::vector<core::pmr::String> listBindings;
                if (globalListBindingForExpr(decl->expr, listBindings))
                    setGlobalListBinding(decl->varName, listBindings);
                CallableDictBindings dictBindings{&core::defaultCpuMemoryResource()};
                if (globalDictBindingForExpr(decl->expr, dictBindings))
                    setGlobalDictBinding(decl->varName, dictBindings);
            } else if (decl->kind == StmtKind::Assign && decl->target != nullptr
                && decl->target->kind == ExprKind::Ident) {
                setGlobalBinding(decl->target->name,
                    decl->assignOp == "=" ? globalBindingForExpr(decl->value)
                                          : core::pmr::String{});
                std::vector<core::pmr::String> listBindings;
                if (decl->assignOp == "=" && globalListBindingForExpr(decl->value, listBindings))
                    setGlobalListBinding(decl->target->name, listBindings);
                CallableDictBindings dictBindings{&core::defaultCpuMemoryResource()};
                if (decl->assignOp == "=" && globalDictBindingForExpr(decl->value, dictBindings))
                    setGlobalDictBinding(decl->target->name, dictBindings);
            }
        }
        // A function can mutate a module global after this lowering pass has
        // finished. Track lexical locals first, then invalidate only facts
        // whose target is actually a module global.
        std::function<void(const core::pmr::Vector<StmtPtr>&, std::vector<std::string>&)>
            collectLocals;
        collectLocals = [&collectLocals](const core::pmr::Vector<StmtPtr>& body,
                            std::vector<std::string>& locals) {
            for (const auto& stmt : body) {
                if (!stmt)
                    continue;
                if (stmt->kind == StmtKind::VarDecl)
                    locals.push_back(stmt->varName.c_str());
                else if (stmt->kind == StmtKind::FuncDecl)
                    locals.push_back(stmt->funcName.c_str());
                else if (stmt->kind == StmtKind::If) {
                    collectLocals(stmt->body, locals);
                    for (const auto& branch : stmt->elifBodies)
                        collectLocals(branch, locals);
                    collectLocals(stmt->elseBody, locals);
                } else if (stmt->kind == StmtKind::While || stmt->kind == StmtKind::For) {
                    collectLocals(stmt->body, locals);
                } else if (stmt->kind == StmtKind::Try) {
                    collectLocals(stmt->tryBody, locals);
                    for (const auto& clause : stmt->exceptClauses)
                        collectLocals(clause.body, locals);
                }
            }
        };
        std::function<bool(const core::pmr::Vector<StmtPtr>&, const std::vector<std::string>&)>
            writesGlobal;
        writesGlobal = [this, &writesGlobal](const core::pmr::Vector<StmtPtr>& body,
                           const std::vector<std::string>& locals) {
            const auto isLocal = [&locals](const core::pmr::String& name) {
                return std::find(locals.begin(), locals.end(), name.c_str()) != locals.end();
            };
            for (const auto& stmt : body) {
                if (!stmt)
                    continue;
                if (stmt->kind == StmtKind::Assign && stmt->target != nullptr
                    && stmt->target->kind == ExprKind::Ident
                    && globals_.find(stmt->target->name.c_str()) != globals_.end()
                    && !isLocal(stmt->target->name))
                    return true;
                if (stmt->kind == StmtKind::VarDecl
                    && globals_.find(stmt->varName.c_str()) != globals_.end()
                    && !isLocal(stmt->varName))
                    return true;
                if (stmt->kind == StmtKind::If) {
                    if (writesGlobal(stmt->body, locals) || writesGlobal(stmt->elseBody, locals))
                        return true;
                    for (const auto& branch : stmt->elifBodies)
                        if (writesGlobal(branch, locals))
                            return true;
                } else if (stmt->kind == StmtKind::While || stmt->kind == StmtKind::For) {
                    if (writesGlobal(stmt->body, locals))
                        return true;
                } else if (stmt->kind == StmtKind::Try) {
                    if (writesGlobal(stmt->tryBody, locals))
                        return true;
                    for (const auto& clause : stmt->exceptClauses)
                        if (writesGlobal(clause.body, locals))
                            return true;
                }
            }
            return false;
        };
        bool hasUnknownGlobalWrite = false;
        for (const auto& decl : module_->decls) {
            if (decl->kind == StmtKind::FuncDecl) {
                std::vector<std::string> locals;
                for (const auto& parameter : decl->paramNames)
                    locals.push_back(parameter.c_str());
                collectLocals(decl->body, locals);
                if (writesGlobal(decl->body, locals)) {
                    hasUnknownGlobalWrite = true;
                    break;
                }
            }
            if (decl->kind == StmtKind::ClassDecl) {
                for (const auto& method : decl->methods) {
                    std::vector<std::string> locals;
                    for (const auto& parameter : method->paramNames)
                        locals.push_back(parameter.c_str());
                    collectLocals(method->body, locals);
                    if (writesGlobal(method->body, locals)) {
                        hasUnknownGlobalWrite = true;
                        break;
                    }
                }
                if (hasUnknownGlobalWrite)
                    break;
            }
            bool topLevelControlWrite = false;
            if (decl->kind == StmtKind::If) {
                const std::vector<std::string> noLocals;
                topLevelControlWrite =
                    writesGlobal(decl->body, noLocals) || writesGlobal(decl->elseBody, noLocals);
                for (const auto& branch : decl->elifBodies)
                    topLevelControlWrite = topLevelControlWrite || writesGlobal(branch, noLocals);
            }
            if (topLevelControlWrite) {
                hasUnknownGlobalWrite = true;
                break;
            }
            if ((decl->kind == StmtKind::While || decl->kind == StmtKind::For)
                && writesGlobal(decl->body, std::vector<std::string>{})) {
                hasUnknownGlobalWrite = true;
                break;
            }
            if (decl->kind == StmtKind::Try
                && writesGlobal(decl->tryBody, std::vector<std::string>{})) {
                hasUnknownGlobalWrite = true;
                break;
            }
        }
        if (hasUnknownGlobalWrite)
            globalBindingProvenance_.clear();
    }

    int ensureGlobal(const core::pmr::String& name)
    {
        auto it = globals_.find(name.c_str());
        if (it != globals_.end())
            return it->second;
        const int slot = globalCount_++;
        globals_.emplace(name.c_str(), slot);
        return slot;
    }

    int emit(DirectFunction& function,
        TypedIrOpcode opcode,
        int a = 0,
        int b = 0,
        std::int64_t i64 = 0,
        core::pmr::String text = {},
        SourcePos source = {})
    {
        TypedIrOperationRecord operation;
        operation.opcode = opcode;
        operation.operation = directOperationKind(opcode);
        operation.a = a;
        operation.b = b;
        operation.i64 = i64;
        operation.text = std::move(text);
        operation.source = std::move(source);
        operation.callKind = directCallKind(opcode);
        operation.effectFlags = directEffects(opcode);
        function.ir.effectFlags |= operation.effectFlags;
        function.operations.push_back(std::move(operation));
        return static_cast<int>(function.operations.size()) - 1;
    }

    int emitJump(DirectFunction& function, TypedIrOpcode opcode, SourcePos source = {})
    {
        emit(function, opcode, 0, 0, 0, {}, std::move(source));
        return static_cast<int>(function.operations.size()) - 1;
    }

    void patch(DirectFunction& function, int pc, int target)
    {
        function.operations[static_cast<std::size_t>(pc)].a = target;
    }

    DirectLocal* findLocal(DirectFunction& function, const core::pmr::String& name)
    {
        return function.scope != nullptr ? function.scope->find(name) : nullptr;
    }

    bool listBindingForExpr(
        DirectFunction& function, const ExprPtr& expr, std::vector<core::pmr::String>& bindings)
    {
        bindings.clear();
        if (!expr)
            return false;
        if (expr->kind == ExprKind::ListLit) {
            bindings.reserve(expr->args.size());
            for (const auto& item : expr->args) {
                const auto binding = bindingForExpr(function, item);
                if (binding.empty()) {
                    bindings.clear();
                    return false;
                }
                bindings.push_back(binding);
            }
            return true;
        }
        if (expr->kind == ExprKind::Ident) {
            if (auto* local = findLocal(function, expr->name)) {
                bindings = local->callableListBindings;
                return !bindings.empty();
            }
            for (const auto& capture : function.captures) {
                if (capture.name == expr->name) {
                    bindings = capture.callableListBindings;
                    return !bindings.empty();
                }
            }
            const auto key = core::pmr::String(expr->name, &core::defaultCpuMemoryResource());
            const auto global = globalCallableListBindings_.find(key);
            if (global != globalCallableListBindings_.end()) {
                bindings.assign(global->second.begin(), global->second.end());
                return !bindings.empty();
            }
        }
        return false;
    }

    bool dictBindingForExpr(
        DirectFunction& function, const ExprPtr& expr, CallableDictBindings& bindings)
    {
        bindings.clear();
        if (!expr)
            return false;
        if (expr->kind == ExprKind::DictLit) {
            for (std::size_t index = 0; index < expr->dictKeys.size(); ++index) {
                const auto& key = expr->dictKeys[index];
                if (!key || key->kind != ExprKind::StringLit) {
                    bindings.clear();
                    return false;
                }
                const auto binding = bindingForExpr(function, expr->dictValues[index]);
                if (binding.empty()) {
                    bindings.clear();
                    return false;
                }
                const auto existing =
                    std::find_if(bindings.begin(), bindings.end(), [&key](const auto& item) {
                        return item.first == key->strVal;
                    });
                if (existing != bindings.end())
                    existing->second = binding;
                else
                    bindings.emplace_back(key->strVal, binding);
            }
            return !bindings.empty();
        }
        if (expr->kind == ExprKind::Ident) {
            if (auto* local = findLocal(function, expr->name)) {
                bindings = local->callableDictBindings;
                return !bindings.empty();
            }
            for (const auto& capture : function.captures) {
                if (capture.name == expr->name) {
                    bindings = capture.callableDictBindings;
                    return !bindings.empty();
                }
            }
            const auto key = core::pmr::String(expr->name, &core::defaultCpuMemoryResource());
            for (const auto& global : globalCallableDictBindings_) {
                if (global.first == key) {
                    bindings = global.second;
                    return !bindings.empty();
                }
            }
        }
        return false;
    }

    core::pmr::String bindingForExpr(DirectFunction& function, const ExprPtr& expr)
    {
        if (!expr)
            return {};
        if (expr->kind == ExprKind::Ident) {
            if (auto* local = findLocal(function, expr->name))
                return local->callableBindingId;
            for (const auto& capture : function.captures) {
                if (capture.name == expr->name)
                    return capture.callableBindingId;
            }
            const auto global = globalBindingProvenance_.find(expr->name.c_str());
            if (global != globalBindingProvenance_.end())
                return global->second;
            return {};
        }
        if (expr->kind == ExprKind::Field && expr->left != nullptr
            && expr->left->kind == ExprKind::Ident) {
            const auto qualified = expr->left->name + "." + expr->name;
            const auto binding = qualifiedCallableBindings_.find(qualified.c_str());
            if (binding != qualifiedCallableBindings_.end())
                return binding->second;
        }
        if (expr->kind == ExprKind::Field && expr->left != nullptr
            && expr->left->inferredType != nullptr
            && expr->left->inferredType->kind == TypeKind::Class) {
            core::pmr::String target(
                expr->left->inferredType->name, &core::defaultCpuMemoryResource());
            target += ".";
            target += expr->name;
            for (const auto& binding : moduleSnapshot_.callableBindings) {
                if (binding.targetId == target)
                    return binding.bindingId;
            }
        }
        if (expr->kind == ExprKind::Index && expr->right != nullptr
            && expr->right->kind == ExprKind::IntLit && expr->right->intVal >= 0) {
            std::vector<core::pmr::String> bindings;
            if (listBindingForExpr(function, expr->left, bindings)
                && expr->right->intVal < static_cast<std::int64_t>(bindings.size()))
                return bindings[static_cast<std::size_t>(expr->right->intVal)];
        }
        if (expr->kind == ExprKind::Index && expr->right != nullptr
            && expr->right->kind == ExprKind::StringLit) {
            CallableDictBindings bindings{&core::defaultCpuMemoryResource()};
            if (dictBindingForExpr(function, expr->left, bindings)) {
                const auto found =
                    std::find_if(bindings.begin(), bindings.end(), [&expr](const auto& item) {
                        return item.first == expr->right->strVal;
                    });
                if (found != bindings.end())
                    return found->second;
            }
        }
        return {};
    }

    core::pmr::String syntacticBindingForCall(
        DirectFunction& function, const ExprPtr& expression, int argumentCount)
    {
        if (!expression || expression->kind != ExprKind::Field || expression->left == nullptr)
            return {};
        core::pmr::String target{&core::defaultCpuMemoryResource()};
        TypedIrCallableOrigin origin = TypedIrCallableOrigin::HostCapability;
        bool importedTarget = false;
        TypePtr receiverType;
        if (expression->left->kind == ExprKind::Ident) {
            const auto imported = importedModuleAliases_.find(expression->left->name.c_str());
            if (imported != importedModuleAliases_.end()
                && externalModuleContractHashes_.find(imported->second.c_str())
                    != externalModuleContractHashes_.end()) {
                target = imported->second;
                target += ".";
                target += expression->name;
                importedTarget = true;
            }
        }
        if (!importedTarget) {
            receiverType = expression->left->inferredType;
            if (expression->left->kind == ExprKind::Ident) {
                if (const auto* local = findLocal(function, expression->left->name))
                    if (local->type != nullptr)
                        receiverType = local->type;
            }
            if (receiverType == nullptr || receiverType->name.empty()
                || receiverType->name == "any") {
                const core::pmr::String suffix =
                    core::pmr::String(".", &core::defaultCpuMemoryResource()) + expression->name;
                const TypedIrCallableBinding* matched = nullptr;
                const auto consider = [&](const TypedIrCallableBinding& contract) {
                    if (contract.origin != TypedIrCallableOrigin::ReflectionMember
                        || contract.fixedArity != argumentCount
                        || contract.targetId.size() <= suffix.size()
                        || contract.targetId.compare(
                               contract.targetId.size() - suffix.size(), suffix.size(), suffix)
                            != 0)
                        return true;
                    if (matched != nullptr && matched->targetId != contract.targetId)
                        return false;
                    matched = &contract;
                    return true;
                };
                for (const auto& contract : moduleSnapshot_.callableBindings)
                    if (!consider(contract))
                        return {};
                for (const auto& contract : synthesizedCallableBindings_)
                    if (!consider(contract))
                        return {};
                if (matched != nullptr)
                    return matched->bindingId;
            }
            if (receiverType == nullptr || receiverType->name.empty() || receiverType->name == "any"
                || receiverType->name == "int" || receiverType->name == "int64"
                || receiverType->name == "float" || receiverType->name == "bool"
                || receiverType->name == "string" || receiverType->name == "bytes")
                return {};
            target = receiverType->name;
            target += ".";
            target += expression->name;
            origin = TypedIrCallableOrigin::ReflectionMember;
        }
        // Fully-qualified reflected object types (for example
        // `arp_scene.Scene`) already carry the module-qualified contract.
        // Resolve that exact generated binding before the legacy module-marker
        // synthesis path, which only applies to unqualified type names.
        if (origin == TypedIrCallableOrigin::ReflectionMember) {
            for (const auto& contract : moduleSnapshot_.callableBindings) {
                if (contract.origin == TypedIrCallableOrigin::ReflectionMember
                    && contract.targetId == target && contract.fixedArity == argumentCount) {
                    return contract.bindingId;
                }
            }
            // A field annotation may retain an imported type's short name.
            // Resolve its exact qualified member contract, preserving the
            // generated signature/identity instead of inventing a new ABI.
            const auto suffix = core::pmr::String(".") + target;
            const TypedIrCallableBinding* qualified = nullptr;
            for (const auto& contract : moduleSnapshot_.callableBindings) {
                if (contract.origin != TypedIrCallableOrigin::ReflectionMember
                    || contract.fixedArity != argumentCount
                    || contract.targetId.size() <= suffix.size()
                    || contract.targetId.compare(
                           contract.targetId.size() - suffix.size(), suffix.size(), suffix)
                        != 0)
                    continue;
                if (qualified && qualified->targetId != contract.targetId) {
                    setError(ScriptDiagnosticCode::TypedIrInvalid,
                        "ambiguous imported reflected member contract",
                        expression->pos);
                    return {};
                }
                qualified = &contract;
            }
            if (qualified)
                return qualified->bindingId;
            if (receiverType && receiverType->reflectedClass) {
                const auto* declared = receiverType->reflectedClass;
                const auto method = declared->get_field(expression->name);
                if (method.is<reflection::function>()) {
                    const auto resolveBase =
                        [&](const auto& self,
                            const reflection::class_info* info) -> core::pmr::String {
                        for (const auto* base : info->bases()) {
                            const auto baseSuffix =
                                core::pmr::String(".") + base->name() + "." + expression->name;
                            for (const auto& contract : moduleSnapshot_.callableBindings)
                                if (contract.origin == TypedIrCallableOrigin::ReflectionMember
                                    && contract.fixedArity == argumentCount
                                    && contract.targetId.size() > baseSuffix.size()
                                    && contract.targetId.compare(
                                           contract.targetId.size() - baseSuffix.size(),
                                           baseSuffix.size(),
                                           baseSuffix)
                                        == 0)
                                    return contract.bindingId;
                            auto inherited = self(self, base);
                            if (!inherited.empty())
                                return inherited;
                        }
                        return {};
                    };
                    auto inherited = resolveBase(resolveBase, declared);
                    if (!inherited.empty())
                        return inherited;
                }
            }
        }
        auto separator = target.find('.');
        if (separator == core::pmr::String::npos)
            return {};
        auto moduleName = target.substr(0u, separator);
        auto contractHash = externalModuleContractHashes_.find(moduleName.c_str());
        if (contractHash == externalModuleContractHashes_.end()
            && origin == TypedIrCallableOrigin::ReflectionMember) {
            core::pmr::String classPattern(".", &core::defaultCpuMemoryResource());
            classPattern += moduleName;
            classPattern += ".";
            for (const auto& contract : moduleSnapshot_.callableBindings) {
                const auto classPosition = contract.targetId.find(classPattern);
                if (classPosition == core::pmr::String::npos)
                    continue;
                const auto resolvedModule = contract.targetId.substr(0u, classPosition);
                const auto resolvedHash =
                    externalModuleContractHashes_.find(resolvedModule.c_str());
                if (resolvedHash == externalModuleContractHashes_.end())
                    continue;
                target = resolvedModule + "." + target;
                moduleName = resolvedModule;
                contractHash = resolvedHash;
                break;
            }
        }
        if (contractHash == externalModuleContractHashes_.end())
            return {};
        const auto existing = std::find_if(synthesizedCallableBindings_.begin(),
            synthesizedCallableBindings_.end(),
            [&target](const auto& binding) {
                return binding.targetId == target;
            });
        if (existing != synthesizedCallableBindings_.end()) {
            if (existing->fixedArity != argumentCount)
                setError(ScriptDiagnosticCode::TypedIrInvalid,
                    "external callable is used with inconsistent arity",
                    expression->pos);
            return existing->bindingId;
        }
        TypedIrCallableBinding binding;
        binding.bindingId = "arp-bound:";
        binding.bindingId += target;
        binding.bindingId += "/arity:";
        binding.bindingId += std::to_string(argumentCount);
        binding.bindingId += "/v1";
        binding.origin = origin;
        binding.targetId = target;
        binding.signatureHash = contractHash->second;
        binding.fixedArity = argumentCount;
        binding.lifetimeVersion = 1u;
        const auto bindingId = binding.bindingId;
        synthesizedCallableBindings_.push_back(std::move(binding));
        return bindingId;
    }

    core::pmr::String syntacticTargetForCall(DirectFunction& function, const ExprPtr& expression)
    {
        if (!expression || expression->kind != ExprKind::Field || expression->left == nullptr)
            return {};
        if (expression->left->kind == ExprKind::Ident) {
            const auto imported = importedModuleAliases_.find(expression->left->name.c_str());
            if (imported != importedModuleAliases_.end()) {
                auto target = imported->second;
                target += ".";
                target += expression->name;
                return target;
            }
            if (const auto* local = findLocal(function, expression->left->name)) {
                if (local->type != nullptr && !local->type->name.empty()) {
                    auto target = local->type->name;
                    target += ".";
                    target += expression->name;
                    return target;
                }
            }
        }
        if (expression->left->inferredType != nullptr
            && !expression->left->inferredType->name.empty()) {
            auto target = expression->left->inferredType->name;
            target += ".";
            target += expression->name;
            return target;
        }
        return {};
    }

    bool globalListBindingForExpr(
        const ExprPtr& expr, std::vector<core::pmr::String>& bindings) const
    {
        bindings.clear();
        if (!expr)
            return false;
        if (expr->kind == ExprKind::ListLit) {
            bindings.reserve(expr->args.size());
            for (const auto& item : expr->args) {
                core::pmr::String binding;
                if (item && item->kind == ExprKind::Ident) {
                    const auto global = globalBindingProvenance_.find(item->name.c_str());
                    if (global != globalBindingProvenance_.end())
                        binding = global->second;
                } else if (item && item->kind == ExprKind::Field && item->left != nullptr
                    && item->left->kind == ExprKind::Ident) {
                    const auto qualified = item->left->name + "." + item->name;
                    const auto qualifiedBinding =
                        qualifiedCallableBindings_.find(qualified.c_str());
                    if (qualifiedBinding != qualifiedCallableBindings_.end())
                        binding = qualifiedBinding->second;
                }
                if (binding.empty()) {
                    bindings.clear();
                    return false;
                }
                bindings.push_back(binding);
            }
            return !bindings.empty();
        }
        if (expr->kind == ExprKind::Ident) {
            const auto key = core::pmr::String(expr->name, &core::defaultCpuMemoryResource());
            const auto global = globalCallableListBindings_.find(key);
            if (global != globalCallableListBindings_.end()) {
                bindings.assign(global->second.begin(), global->second.end());
                return !bindings.empty();
            }
        }
        return false;
    }

    bool globalDictBindingForExpr(const ExprPtr& expr, CallableDictBindings& bindings) const
    {
        bindings.clear();
        if (!expr)
            return false;
        if (expr->kind == ExprKind::DictLit) {
            for (std::size_t index = 0; index < expr->dictKeys.size(); ++index) {
                const auto& key = expr->dictKeys[index];
                if (!key || key->kind != ExprKind::StringLit) {
                    bindings.clear();
                    return false;
                }
                core::pmr::String binding;
                const auto& value = expr->dictValues[index];
                if (value && value->kind == ExprKind::Ident) {
                    const auto global = globalBindingProvenance_.find(value->name.c_str());
                    if (global != globalBindingProvenance_.end())
                        binding = global->second;
                } else if (value && value->kind == ExprKind::Field && value->left != nullptr
                    && value->left->kind == ExprKind::Ident) {
                    const auto qualified = value->left->name + "." + value->name;
                    const auto qualifiedBinding =
                        qualifiedCallableBindings_.find(qualified.c_str());
                    if (qualifiedBinding != qualifiedCallableBindings_.end())
                        binding = qualifiedBinding->second;
                }
                if (binding.empty()) {
                    bindings.clear();
                    return false;
                }
                const auto existing =
                    std::find_if(bindings.begin(), bindings.end(), [&key](const auto& item) {
                        return item.first == key->strVal;
                    });
                if (existing != bindings.end())
                    existing->second = binding;
                else
                    bindings.emplace_back(key->strVal, binding);
            }
            return !bindings.empty();
        }
        if (expr->kind == ExprKind::Ident) {
            const auto key = core::pmr::String(expr->name, &core::defaultCpuMemoryResource());
            for (const auto& global : globalCallableDictBindings_) {
                if (global.first == key) {
                    bindings = global.second;
                    return !bindings.empty();
                }
            }
        }
        return false;
    }

    core::pmr::String globalBindingForExpr(const ExprPtr& expr) const
    {
        if (!expr)
            return {};
        if (expr->kind == ExprKind::Ident) {
            const auto binding = globalBindingProvenance_.find(expr->name.c_str());
            return binding == globalBindingProvenance_.end() ? core::pmr::String{}
                                                             : binding->second;
        }
        if (expr->kind == ExprKind::Field && expr->left != nullptr
            && expr->left->kind == ExprKind::Ident) {
            const auto qualified = expr->left->name + "." + expr->name;
            const auto binding = qualifiedCallableBindings_.find(qualified.c_str());
            if (binding != qualifiedCallableBindings_.end())
                return binding->second;
        }
        return {};
    }

    void clearBinding(DirectLocal* local)
    {
        if (local == nullptr)
            return;
        local->callableBindingId.clear();
        local->callableListBindings.clear();
        local->callableDictBindings.clear();
        // A capture is a live cell alias. Once the source local is overwritten,
        // a previously compiled child must no longer claim a fixed contract.
        for (const auto& function : functions_) {
            for (auto& capture : function->captures) {
                if (capture.source == local) {
                    capture.callableBindingId.clear();
                    capture.callableListBindings.clear();
                    capture.callableDictBindings.clear();
                }
            }
        }
    }

    void setBinding(DirectLocal* local, const core::pmr::String& binding)
    {
        if (local == nullptr)
            return;
        local->callableBindingId = binding;
        local->callableListBindings.clear();
        local->callableDictBindings.clear();
    }

    void setListBinding(DirectLocal* local, const std::vector<core::pmr::String>& bindings)
    {
        if (local == nullptr)
            return;
        local->callableBindingId.clear();
        local->callableListBindings = bindings;
        local->callableDictBindings.clear();
    }

    void setDictBinding(DirectLocal* local, const CallableDictBindings& bindings)
    {
        if (local == nullptr)
            return;
        local->callableBindingId.clear();
        local->callableListBindings.clear();
        local->callableDictBindings = bindings;
    }

    void clearAllCallableContainerBindings()
    {
        for (const auto& function : functions_) {
            for (const auto& scope : function->scopes)
                for (const auto& local : scope->locals) {
                    local->callableListBindings.clear();
                    local->callableDictBindings.clear();
                }
            for (auto& capture : function->captures) {
                capture.callableListBindings.clear();
                capture.callableDictBindings.clear();
            }
        }
        globalCallableListBindings_.clear();
        globalCallableDictBindings_.clear();
    }

    void clearGlobalBinding(const core::pmr::String& name)
    {
        globalBindingProvenance_.erase(name.c_str());
        const auto key = core::pmr::String(name, &core::defaultCpuMemoryResource());
        globalCallableListBindings_.erase(key);
        globalCallableDictBindings_.erase(std::remove_if(globalCallableDictBindings_.begin(),
                                              globalCallableDictBindings_.end(),
                                              [&key](const auto& item) {
                                                  return item.first == key;
                                              }),
            globalCallableDictBindings_.end());
    }

    void setGlobalBinding(const core::pmr::String& name, const core::pmr::String& binding)
    {
        if (binding.empty())
            clearGlobalBinding(name);
        else {
            globalBindingProvenance_[name.c_str()] = binding;
            const auto key = core::pmr::String(name, &core::defaultCpuMemoryResource());
            globalCallableListBindings_.erase(key);
            globalCallableDictBindings_.erase(std::remove_if(globalCallableDictBindings_.begin(),
                                                  globalCallableDictBindings_.end(),
                                                  [&key](const auto& item) {
                                                      return item.first == key;
                                                  }),
                globalCallableDictBindings_.end());
        }
    }

    void setGlobalListBinding(
        const core::pmr::String& name, const std::vector<core::pmr::String>& bindings)
    {
        const auto key = core::pmr::String(name, &core::defaultCpuMemoryResource());
        if (bindings.empty()) {
            globalCallableListBindings_.erase(key);
            return;
        }
        core::pmr::Vector<core::pmr::String> copied{&core::defaultCpuMemoryResource()};
        copied.assign(bindings.begin(), bindings.end());
        globalCallableListBindings_.insert_or_assign(key, std::move(copied));
        globalCallableDictBindings_.erase(std::remove_if(globalCallableDictBindings_.begin(),
                                              globalCallableDictBindings_.end(),
                                              [&key](const auto& item) {
                                                  return item.first == key;
                                              }),
            globalCallableDictBindings_.end());
    }

    void setGlobalDictBinding(const core::pmr::String& name, const CallableDictBindings& bindings)
    {
        const auto key = core::pmr::String(name, &core::defaultCpuMemoryResource());
        globalBindingProvenance_.erase(name.c_str());
        globalCallableListBindings_.erase(key);
        globalCallableDictBindings_.erase(std::remove_if(globalCallableDictBindings_.begin(),
                                              globalCallableDictBindings_.end(),
                                              [&key](const auto& item) {
                                                  return item.first == key;
                                              }),
            globalCallableDictBindings_.end());
        if (bindings.empty())
            return;
        CallableDictBindings copied{&core::defaultCpuMemoryResource()};
        copied.assign(bindings.begin(), bindings.end());
        globalCallableDictBindings_.emplace_back(key, std::move(copied));
    }

    struct BindingSnapshot {
        std::vector<std::pair<DirectLocal*, core::pmr::String>> locals;
        std::vector<std::pair<DirectCapture*, core::pmr::String>> captures;
        std::unordered_map<std::string, core::pmr::String> globals;
    };

    BindingSnapshot snapshotBindings(DirectFunction& function)
    {
        BindingSnapshot snapshot;
        for (const auto& scope : function.scopes) {
            for (const auto& local : scope->locals)
                snapshot.locals.emplace_back(local.get(), local->callableBindingId);
        }
        for (auto& capture : function.captures)
            snapshot.captures.emplace_back(&capture, capture.callableBindingId);
        snapshot.globals = globalBindingProvenance_;
        return snapshot;
    }

    void restoreBindings(const BindingSnapshot& snapshot)
    {
        for (const auto& item : snapshot.locals)
            item.first->callableBindingId = item.second;
        for (const auto& item : snapshot.captures)
            item.first->callableBindingId = item.second;
        globalBindingProvenance_ = snapshot.globals;
        // Container facts are intentionally not part of the CFG merge
        // contract yet. Never let a prior branch's list fact leak into the
        // next path.
        for (const auto& function : functions_) {
            for (const auto& scope : function->scopes)
                for (const auto& local : scope->locals) {
                    local->callableListBindings.clear();
                    local->callableDictBindings.clear();
                }
            for (auto& capture : function->captures) {
                capture.callableListBindings.clear();
                capture.callableDictBindings.clear();
            }
        }
        globalCallableListBindings_.clear();
        globalCallableDictBindings_.clear();
    }

    static const core::pmr::String* findGlobalBinding(
        const std::unordered_map<std::string, core::pmr::String>& globals, const std::string& name)
    {
        const auto it = globals.find(name);
        return it == globals.end() ? nullptr : &it->second;
    }

    static bool sameBinding(const core::pmr::String* lhs, const core::pmr::String* rhs)
    {
        if (lhs == nullptr || rhs == nullptr)
            return lhs == rhs;
        return *lhs == *rhs;
    }

    void mergeBindingSnapshots(DirectFunction& function,
        const BindingSnapshot& baseline,
        const std::vector<BindingSnapshot>& paths)
    {
        if (paths.empty()) {
            restoreBindings(baseline);
            return;
        }
        restoreBindings(baseline);
        for (std::size_t index = 0; index < baseline.locals.size(); ++index) {
            const auto* agreed = &paths.front().locals[index].second;
            bool consistent = true;
            for (std::size_t pathIndex = 1; pathIndex < paths.size(); ++pathIndex) {
                const auto& path = paths[pathIndex];
                if (index >= path.locals.size()
                    || !sameBinding(agreed, &path.locals[index].second)) {
                    consistent = false;
                    break;
                }
            }
            if (consistent)
                baseline.locals[index].first->callableBindingId = *agreed;
            else
                clearBinding(baseline.locals[index].first);
        }
        for (std::size_t index = 0; index < baseline.captures.size(); ++index) {
            const auto* agreed = &paths.front().captures[index].second;
            bool consistent = true;
            for (std::size_t pathIndex = 1; pathIndex < paths.size(); ++pathIndex) {
                const auto& path = paths[pathIndex];
                if (index >= path.captures.size()
                    || !sameBinding(agreed, &path.captures[index].second)) {
                    consistent = false;
                    break;
                }
            }
            if (consistent)
                baseline.captures[index].first->callableBindingId = *agreed;
            else {
                baseline.captures[index].first->callableBindingId.clear();
                if (baseline.captures[index].first->source != nullptr)
                    clearBinding(baseline.captures[index].first->source);
            }
        }
        std::unordered_map<std::string, core::pmr::String> merged;
        for (const auto& path : paths) {
            for (const auto& item : path.globals)
                merged.emplace(item.first, core::pmr::String{});
        }
        std::vector<std::string> globalNames;
        globalNames.reserve(merged.size());
        for (const auto& item : merged)
            globalNames.push_back(item.first);
        for (const auto& name : globalNames) {
            const auto* agreed = findGlobalBinding(paths.front().globals, name);
            bool consistent = true;
            for (std::size_t pathIndex = 1; pathIndex < paths.size(); ++pathIndex) {
                const auto* candidate = findGlobalBinding(paths[pathIndex].globals, name);
                if (!sameBinding(agreed, candidate)) {
                    consistent = false;
                    break;
                }
            }
            if (!consistent || agreed == nullptr || agreed->empty())
                merged.erase(name);
            else
                merged[name] = *agreed;
        }
        globalBindingProvenance_ = std::move(merged);
        // Locals introduced in one branch have no guaranteed value after the
        // merge. Clear them even when the branch itself carried a binding.
        for (const auto& scope : function.scopes) {
            for (const auto& local : scope->locals) {
                const auto known = std::find_if(baseline.locals.begin(),
                    baseline.locals.end(),
                    [localPtr = local.get()](const auto& item) {
                        return item.first == localPtr;
                    });
                if (known == baseline.locals.end())
                    clearBinding(local.get());
                else
                    local->callableListBindings.clear();
            }
        }
    }

    void clearBindingTarget(DirectFunction& function, const core::pmr::String& name)
    {
        if (auto* local = findLocal(function, name)) {
            clearBinding(local);
            return;
        }
        for (auto& capture : function.captures) {
            if (capture.name != name)
                continue;
            capture.callableBindingId.clear();
            capture.callableListBindings.clear();
            capture.callableDictBindings.clear();
            if (capture.source != nullptr)
                clearBinding(capture.source);
            return;
        }
        if (globals_.find(name.c_str()) != globals_.end())
            clearGlobalBinding(name);
    }

    void clearBindingsWritten(DirectFunction& function, const core::pmr::Vector<StmtPtr>& body)
    {
        std::function<void(const StmtPtr&)> visit = [&](const StmtPtr& stmt) {
            if (!stmt)
                return;
            if (stmt->kind == StmtKind::Assign && stmt->target != nullptr
                && stmt->target->kind == ExprKind::Ident) {
                clearBindingTarget(function, stmt->target->name);
            } else if (stmt->kind == StmtKind::VarDecl) {
                clearBindingTarget(function, stmt->varName);
            }
            if (stmt->kind == StmtKind::If) {
                for (const auto& child : stmt->body)
                    visit(child);
                for (const auto& branch : stmt->elifBodies)
                    for (const auto& child : branch)
                        visit(child);
                for (const auto& child : stmt->elseBody)
                    visit(child);
            } else if (stmt->kind == StmtKind::While || stmt->kind == StmtKind::For
                || stmt->kind == StmtKind::Try) {
                for (const auto& child : stmt->body)
                    visit(child);
                for (const auto& child : stmt->tryBody)
                    visit(child);
                for (const auto& clause : stmt->exceptClauses)
                    for (const auto& child : clause.body)
                        visit(child);
            }
        };
        for (const auto& stmt : body)
            visit(stmt);
    }

    void emitConversion(DirectFunction& function,
        const TypePtr& actual,
        const TypedIrTypeRef& expected,
        const SourcePos& source)
    {
        const auto actualType = directType(actual).kind;
        if (expected.kind == TypedIrType::Int64 && actualType == TypedIrType::Int)
            emit(function, TypedIrOpcode::ConvertInt64, 0, 0, 0, {}, source);
        else if (expected.kind == TypedIrType::Float
            && (actualType == TypedIrType::Int || actualType == TypedIrType::Int64))
            emit(function, TypedIrOpcode::ConvertFloat, 0, 0, 0, {}, source);
    }

    void emitConversion(DirectFunction& function,
        const TypePtr& actual,
        const TypePtr& expected,
        const SourcePos& source)
    {
        emitConversion(function, actual, directType(expected), source);
    }

    std::vector<DirectCapture> capturesFor(
        DirectFunction& function, const core::pmr::Vector<StmtPtr>& body)
    {
        std::vector<core::pmr::String> referenced;
        for (const auto& stmt : body)
            collectDirectStmtIds(stmt, referenced);
        std::vector<DirectCapture> captures;
        for (DirectScope* scope = function.scope; scope != nullptr; scope = scope->parent) {
            for (const auto& local : scope->locals) {
                const bool referencedHere =
                    std::find(referenced.begin(), referenced.end(), local->name)
                    != referenced.end();
                if (!referencedHere)
                    continue;
                const bool already = std::find_if(captures.begin(),
                                         captures.end(),
                                         [&local](const DirectCapture& capture) {
                                             return capture.name == local->name;
                                         })
                    != captures.end();
                if (!already) {
                    local->captured = true;
                    DirectCapture capture;
                    capture.name = local->name;
                    capture.slot = static_cast<int>(captures.size());
                    capture.source = local.get();
                    capture.callableBindingId = local->callableBindingId;
                    capture.callableListBindings = local->callableListBindings;
                    capture.callableDictBindings = local->callableDictBindings;
                    captures.push_back(std::move(capture));
                }
            }
        }
        return captures;
    }

    std::vector<DirectCapture> capturesForExpr(DirectFunction& function, const ExprPtr& expression)
    {
        std::vector<core::pmr::String> referenced;
        collectDirectIds(expression, referenced);
        std::vector<DirectCapture> captures;
        for (DirectScope* scope = function.scope; scope != nullptr; scope = scope->parent) {
            for (const auto& local : scope->locals) {
                if (std::find(referenced.begin(), referenced.end(), local->name)
                    == referenced.end())
                    continue;
                const bool already = std::find_if(captures.begin(),
                                         captures.end(),
                                         [&local](const DirectCapture& capture) {
                                             return capture.name == local->name;
                                         })
                    != captures.end();
                if (!already) {
                    local->captured = true;
                    DirectCapture capture;
                    capture.name = local->name;
                    capture.slot = static_cast<int>(captures.size());
                    capture.source = local.get();
                    capture.callableBindingId = local->callableBindingId;
                    capture.callableListBindings = local->callableListBindings;
                    capture.callableDictBindings = local->callableDictBindings;
                    captures.push_back(std::move(capture));
                }
            }
        }
        return captures;
    }

    void compileFunction(const StmtPtr& decl, int index, const std::vector<DirectCapture>& captures)
    {
        DirectFunction& function = *functions_[static_cast<std::size_t>(index)];
        function.captures = captures;
        function.ir.captureCount = static_cast<int>(captures.size());
        function.ir.isGenerator = decl->isGenerator;
        function.pushScope();
        for (std::size_t param = 0; param < decl->paramNames.size(); ++param)
            function.alloc(decl->paramNames[param],
                true,
                param < decl->paramTypes.size() ? decl->paramTypes[param] : TypePtr{});
        compileBlock(function, decl->body);
        if (function.operations.empty()
            || (function.operations.back().opcode != TypedIrOpcode::Return
                && function.operations.back().opcode != TypedIrOpcode::ReturnInt)) {
            if (decl->retType && decl->retType->name == "void")
                emit(function, TypedIrOpcode::Return, 0, 0, 0, {}, decl->pos);
            else {
                emit(function, TypedIrOpcode::LoadNil, 0, 0, 0, {}, decl->pos);
                emit(function, TypedIrOpcode::Return, 1, 0, 0, {}, decl->pos);
            }
        }
        function.popScope();
        // Captures have canonical slots in the Typed IR metadata even though
        // the runtime stores their cells in the separate capture vector.
        function.ir.localCount = function.nextSlot + static_cast<int>(function.captures.size());
        function.ir.maxStack = std::max(1, function.maxStack);
        function.ir.switchability = TypedIrSwitchability::FunctionBoundary;
        for (const auto& op : function.operations) {
            if (op.opcode == TypedIrOpcode::Yield || op.opcode == TypedIrOpcode::Await) {
                if (!op.source.valid()) {
                    function.ir.switchability = TypedIrSwitchability::FunctionBoundary;
                    break;
                }
                function.ir.switchability = TypedIrSwitchability::Safepoint;
            }
        }
        for (const auto& scope : function.scopes) {
            for (const auto& local : scope->locals) {
                TypedIrLocal typed;
                typed.name = local->name;
                typed.slot = local->slot;
                typed.kind = local->parameter ? TypedIrLocalKind::Param : TypedIrLocalKind::Local;
                typed.type = directType(local->type);
                typed.endOperation = static_cast<int>(function.operations.size());
                function.ir.locals.push_back(std::move(typed));
            }
        }
        for (const auto& capture : function.captures) {
            TypedIrLocal typed;
            typed.name = capture.name;
            typed.name += "@capture";
            typed.slot = function.nextSlot + capture.slot;
            typed.kind = TypedIrLocalKind::Capture;
            typed.startOperation = 0;
            typed.endOperation = static_cast<int>(function.operations.size());
            function.ir.locals.push_back(std::move(typed));
        }
    }

    void compileModuleInit()
    {
        StmtPtr synthetic = std::make_shared<Stmt>(StmtKind::FuncDecl, SourcePos{});
        synthetic->funcName = "__module_init__";
        synthetic->retType = makeType(TypeKind::Basic);
        synthetic->retType->name = "int";
        synthetic->body = core::pmr::Vector<StmtPtr>(&core::defaultCpuMemoryResource());
        for (const auto& decl : module_->decls) {
            if (decl->kind != StmtKind::FuncDecl && decl->kind != StmtKind::ClassDecl)
                synthetic->body.push_back(decl);
        }
        const int index = addFunction(synthetic->funcName, synthetic);
        DirectFunction& function = *functions_[static_cast<std::size_t>(index)];
        function.moduleInit = true;
        function.ir.parameterCount = 0;
        compileFunction(synthetic, index, {});
        if (function.operations.size() >= 2
            && function.operations[function.operations.size() - 1].opcode == TypedIrOpcode::Return
            && function.operations[function.operations.size() - 2].opcode
                == TypedIrOpcode::LoadNil) {
            function.operations.pop_back();
            function.operations.pop_back();
        }
        emit(function, TypedIrOpcode::LoadInt, 0, 0, 0, {}, {});
        emit(function, TypedIrOpcode::Return, 1, 0, 0, {}, {});
    }

    void compileBlock(DirectFunction& function, const core::pmr::Vector<StmtPtr>& body)
    {
        for (const auto& stmt : body) {
            if (!ok())
                return;
            compileStmt(function, stmt);
        }
    }

    int conditionJump(DirectFunction& function, const ExprPtr& condition)
    {
        if (condition != nullptr && condition->kind == ExprKind::Binary && condition->op == "<"
            && condition->left != nullptr && condition->left->kind == ExprKind::Ident
            && condition->right != nullptr && condition->right->kind == ExprKind::IntLit) {
            const auto* local = findLocal(function, condition->left->name);
            if (local != nullptr && !local->captured && directIntType(local->type)) {
                const int pc = static_cast<int>(function.operations.size());
                emit(function,
                    TypedIrOpcode::JumpIfLocalIntLtConstFalse,
                    0,
                    local->slot,
                    condition->right->intVal,
                    {},
                    condition->pos);
                return pc;
            }
        }
        compileExpr(function, condition);
        function.pop();
        return emitJump(
            function, TypedIrOpcode::JumpIfFalse, condition ? condition->pos : SourcePos{});
    }

    void compileExpr(DirectFunction& function, const ExprPtr& expr)
    {
        if (!expr || !ok())
            return;
        switch (expr->kind) {
        case ExprKind::IntLit:
            emit(function, TypedIrOpcode::LoadInt, 0, 0, expr->intVal, {}, expr->pos);
            function.push();
            break;
        case ExprKind::Int64Lit:
            emit(function, TypedIrOpcode::LoadInt64, 0, 0, expr->intVal, {}, expr->pos);
            function.push();
            break;
        case ExprKind::FloatLit: {
            std::int64_t bits = 0;
            std::memcpy(&bits, &expr->floatVal, sizeof(bits));
            emit(function, TypedIrOpcode::LoadFloat, 0, 0, bits, {}, expr->pos);
            function.push();
            break;
        }
        case ExprKind::StringLit:
            emit(function, TypedIrOpcode::LoadString, 0, 0, 0, expr->strVal, expr->pos);
            function.push();
            break;
        case ExprKind::BoolLit:
            emit(function, TypedIrOpcode::LoadBool, expr->boolVal ? 1 : 0, 0, 0, {}, expr->pos);
            function.push();
            break;
        case ExprKind::NilLit:
            emit(function, TypedIrOpcode::LoadNil, 0, 0, 0, {}, expr->pos);
            function.push();
            break;
        case ExprKind::Ident: {
            if (auto* local = findLocal(function, expr->name)) {
                if (local->captured) {
                    emit(function, TypedIrOpcode::AddrLocal, local->slot, 0, 0, {}, expr->pos);
                    emit(function, TypedIrOpcode::Deref, 0, 0, 0, {}, expr->pos);
                } else
                    emit(function, TypedIrOpcode::LoadLocal, local->slot, 0, 0, {}, expr->pos);
                function.push();
                break;
            }
            for (const auto& capture : function.captures) {
                if (capture.name == expr->name) {
                    emit(function, TypedIrOpcode::LoadCapture, capture.slot, 0, 0, {}, expr->pos);
                    function.push();
                    return;
                }
            }
            auto fn = functionIndex_.find(expr->name.c_str());
            if (fn != functionIndex_.end()) {
                emit(function, TypedIrOpcode::MakeFuncValue, fn->second, 0, 0, {}, expr->pos);
                function.push();
                break;
            }
            auto global = globals_.find(expr->name.c_str());
            if (global != globals_.end()) {
                emit(function, TypedIrOpcode::LoadGlobal, global->second, 0, 0, {}, expr->pos);
                function.push();
                break;
            }
            setError(ScriptDiagnosticCode::UndeclaredName,
                core::pmr::String("undeclared name: ", &core::defaultCpuMemoryResource())
                    + expr->name,
                expr->pos);
            break;
        }
        case ExprKind::Binary: {
            if (expr->op == "and" || expr->op == "or") {
                compileExpr(function, expr->left);
                const int branch = emitJump(function,
                    expr->op == "and" ? TypedIrOpcode::JumpIfFalse : TypedIrOpcode::JumpIfTrue,
                    expr->pos);
                function.pop();
                compileExpr(function, expr->right);
                const int end = emitJump(function, TypedIrOpcode::Jump, expr->pos);
                patch(function, branch, static_cast<int>(function.operations.size()));
                emit(function,
                    TypedIrOpcode::LoadBool,
                    expr->op == "or" ? 1 : 0,
                    0,
                    0,
                    {},
                    expr->pos);
                function.push();
                patch(function, end, static_cast<int>(function.operations.size()));
                break;
            }
            compileExpr(function, expr->left);
            compileExpr(function, expr->right);
            function.pop(2);
            TypedIrOpcode opcode = TypedIrOpcode::Add;
            if (expr->op == "+") {
            } else if (expr->op == "-")
                opcode = TypedIrOpcode::Sub;
            else if (expr->op == "*")
                opcode = TypedIrOpcode::Mul;
            else if (expr->op == "/")
                opcode = TypedIrOpcode::Div;
            else if (expr->op == "%")
                opcode = TypedIrOpcode::Mod;
            else if (expr->op == "**")
                opcode = TypedIrOpcode::Pow;
            else if (expr->op == "//")
                opcode = TypedIrOpcode::FloorDiv;
            else if (expr->op == "&")
                opcode = TypedIrOpcode::BitAnd;
            else if (expr->op == "|")
                opcode = TypedIrOpcode::BitOr;
            else if (expr->op == "^")
                opcode = TypedIrOpcode::BitXor;
            else if (expr->op == "<<")
                opcode = TypedIrOpcode::ShiftLeft;
            else if (expr->op == ">>")
                opcode = TypedIrOpcode::ShiftRight;
            else if (expr->op == "==")
                opcode = TypedIrOpcode::CmpEq;
            else if (expr->op == "!=")
                opcode = TypedIrOpcode::CmpNe;
            else if (expr->op == "<")
                opcode = TypedIrOpcode::CmpLt;
            else if (expr->op == "<=")
                opcode = TypedIrOpcode::CmpLe;
            else if (expr->op == ">")
                opcode = TypedIrOpcode::CmpGt;
            else if (expr->op == ">=")
                opcode = TypedIrOpcode::CmpGe;
            else {
                setError(ScriptDiagnosticCode::UnexpectedToken,
                    core::pmr::String("unknown operator: ", &core::defaultCpuMemoryResource())
                        + expr->op,
                    expr->pos);
                return;
            }
            if ((directIntType(expr->inferredType) || directBoolType(expr->inferredType))
                && directIntType(expr->left ? expr->left->inferredType : TypePtr{})
                && directIntType(expr->right ? expr->right->inferredType : TypePtr{})) {
                if (opcode == TypedIrOpcode::Add)
                    opcode = TypedIrOpcode::IntAdd;
                else if (opcode == TypedIrOpcode::Sub)
                    opcode = TypedIrOpcode::IntSub;
                else if (opcode == TypedIrOpcode::Mul)
                    opcode = TypedIrOpcode::IntMul;
                else if (opcode == TypedIrOpcode::Div)
                    opcode = TypedIrOpcode::IntDiv;
                else if (opcode == TypedIrOpcode::Mod)
                    opcode = TypedIrOpcode::IntMod;
                else if (opcode == TypedIrOpcode::CmpEq)
                    opcode = TypedIrOpcode::IntCmpEq;
                else if (opcode == TypedIrOpcode::CmpNe)
                    opcode = TypedIrOpcode::IntCmpNe;
                else if (opcode == TypedIrOpcode::CmpLt)
                    opcode = TypedIrOpcode::IntCmpLt;
                else if (opcode == TypedIrOpcode::CmpLe)
                    opcode = TypedIrOpcode::IntCmpLe;
                else if (opcode == TypedIrOpcode::CmpGt)
                    opcode = TypedIrOpcode::IntCmpGt;
                else if (opcode == TypedIrOpcode::CmpGe)
                    opcode = TypedIrOpcode::IntCmpGe;
            }
            emit(function, opcode, 0, 0, 0, {}, expr->pos);
            function.push();
            break;
        }
        case ExprKind::Unary:
            compileExpr(function, expr->left);
            function.pop();
            if (expr->op == "-")
                emit(function, TypedIrOpcode::Neg, 0, 0, 0, {}, expr->pos);
            else if (expr->op == "not")
                emit(function, TypedIrOpcode::Not, 0, 0, 0, {}, expr->pos);
            else if (expr->op == "~") {
                emit(function, TypedIrOpcode::LoadInt, 0, 0, -1, {}, expr->pos);
                function.push();
                emit(function, TypedIrOpcode::BitXor, 0, 0, 0, {}, expr->pos);
                function.pop();
            } else if (expr->op != "+") {
                setError(
                    ScriptDiagnosticCode::UnexpectedToken, "unknown unary operator", expr->pos);
                return;
            }
            function.push();
            break;
        case ExprKind::Await:
            compileExpr(function, expr->left);
            emit(function, TypedIrOpcode::Await, 0, 0, 0, {}, expr->pos);
            clearAllCallableContainerBindings();
            break;
        case ExprKind::Index:
            compileExpr(function, expr->left);
            compileExpr(function, expr->right);
            function.pop(2);
            emit(function, TypedIrOpcode::LoadIndex, 0, 0, 0, {}, expr->pos);
            function.push();
            break;
        case ExprKind::Field: {
            compileExpr(function, expr->left);
            function.pop();
            int methodIndex = -1;
            if (expr->left != nullptr && expr->left->inferredType != nullptr
                && expr->left->inferredType->kind == TypeKind::Class
                && expr->left->inferredType->reflectedClass == nullptr) {
                core::pmr::Set<core::pmr::String> visited{&core::defaultCpuMemoryResource()};
                auto findMethod = [&](auto&& self, const core::pmr::String& className) -> int {
                    if (!visited.emplace(className).second)
                        return -1;
                    for (const auto& declaration : module_->decls) {
                        if (declaration->kind != StmtKind::ClassDecl
                            || declaration->className != className)
                            continue;
                        for (const auto& method : declaration->methods) {
                            if (method->funcName != expr->name)
                                continue;
                            const auto methodIt = functionIndex_.find(
                                (declaration->className + "." + method->funcName).c_str());
                            if (methodIt != functionIndex_.end())
                                return methodIt->second;
                        }
                        if (!declaration->baseClassName.empty())
                            return self(self, declaration->baseClassName);
                        return -1;
                    }
                    return -1;
                };
                methodIndex = findMethod(findMethod, expr->left->inferredType->name);
            }
            if (methodIndex >= 0)
                emit(function,
                    TypedIrOpcode::MakeMethodValue,
                    methodIndex,
                    0,
                    0,
                    expr->name,
                    expr->pos);
            else {
                emit(function, TypedIrOpcode::LoadField, 0, 0, 0, expr->name, expr->pos);
                function.operations.back().callableBindingId = bindingForExpr(function, expr);
            }
            function.push();
            break;
        }
        case ExprKind::ListLit:
            for (const auto& arg : expr->args)
                compileExpr(function, arg);
            function.pop(static_cast<int>(expr->args.size()));
            emit(function,
                TypedIrOpcode::MakeList,
                0,
                static_cast<int>(expr->args.size()),
                0,
                {},
                expr->pos);
            function.push();
            break;
        case ExprKind::DictLit:
            for (std::size_t i = 0; i < expr->dictKeys.size(); ++i) {
                compileExpr(function, expr->dictKeys[i]);
                compileExpr(function, expr->dictValues[i]);
            }
            function.pop(static_cast<int>(expr->dictKeys.size() * 2));
            emit(function,
                TypedIrOpcode::MakeDict,
                0,
                static_cast<int>(expr->dictKeys.size()),
                0,
                {},
                expr->pos);
            function.push();
            break;
        case ExprKind::AsCast: {
            compileExpr(function, expr->left);
            const auto target = expr->castType ? expr->castType->name : core::pmr::String{};
            TypedIrOpcode opcode = target == "int" ? TypedIrOpcode::ConvertInt
                : target == "int64"                ? TypedIrOpcode::ConvertInt64
                : target == "float"                ? TypedIrOpcode::ConvertFloat
                : target == "string"               ? TypedIrOpcode::ConvertString
                : target == "bool"                 ? TypedIrOpcode::ConvertBool
                : target == "bytes"                ? TypedIrOpcode::ConvertBytes
                                                   : TypedIrOpcode::OpcodeCount;
            if (opcode == TypedIrOpcode::OpcodeCount) {
                setError(
                    ScriptDiagnosticCode::TypeMismatch, "unsupported conversion target", expr->pos);
                return;
            }
            emit(function, opcode, 0, 0, 0, {}, expr->pos);
            break;
        }
        case ExprKind::Call:
            compileCall(function, expr);
            break;
        case ExprKind::Lambda: {
            const auto captures = capturesForExpr(function, expr->left);
            auto lambda = std::make_shared<Stmt>(StmtKind::FuncDecl, expr->pos);
            lambda->funcName = "__lambda_" + std::to_string(functions_.size());
            lambda->paramNames = expr->paramNames;
            lambda->paramTypes = expr->paramTypes;
            lambda->retType = expr->retType;
            auto result = std::make_shared<Stmt>(StmtKind::Return, expr->pos);
            result->expr = expr->left;
            lambda->body.push_back(std::move(result));
            const int index = addFunction(lambda->funcName, lambda);
            compileFunction(lambda, index, captures);
            for (const auto& capture : captures) {
                if (auto* local = findLocal(function, capture.name)) {
                    emit(function, TypedIrOpcode::AddrLocal, local->slot, 0, 0, {}, expr->pos);
                    function.push();
                }
            }
            if (captures.empty()) {
                emit(function, TypedIrOpcode::MakeFuncValue, index, 0, 0, {}, expr->pos);
            } else {
                emit(function,
                    TypedIrOpcode::MakeClosure,
                    index,
                    static_cast<int>(captures.size()),
                    0,
                    {},
                    expr->pos);
                function.pop(static_cast<int>(captures.size()));
            }
            function.push();
            break;
        }
        default:
            setError(ScriptDiagnosticCode::UnexpectedToken,
                "unsupported expression in direct typed lowering",
                expr->pos);
            break;
        }
    }

    void compileCall(DirectFunction& function, const ExprPtr& expr)
    {
        if (!expr->left)
            return;
        const bool named = expr->left->kind == ExprKind::Ident;
        const core::pmr::String name = named ? expr->left->name : core::pmr::String{};
        auto classIt = classFields_.find(name.c_str());
        auto fnIt = functionIndex_.find(name.c_str());
        for (const auto& arg : expr->args)
            compileExpr(function, arg);
        const int count = static_cast<int>(expr->args.size());
        if (fnIt == functionIndex_.end() && expr->left->inferredType != nullptr
            && expr->left->inferredType->kind == TypeKind::Func) {
            const auto& parameters = expr->left->inferredType->params;
            for (int index = 0; index < count && index < static_cast<int>(parameters.size());
                ++index) {
                emitConversion(function,
                    expr->args[static_cast<std::size_t>(index)]->inferredType,
                    parameters[static_cast<std::size_t>(index)],
                    expr->pos);
            }
        }
        const auto invalidateCallableLists = [this]() {
            clearAllCallableContainerBindings();
        };
        if (classIt != classFields_.end()) {
            const auto constructor = constructors_.find(name.c_str());
            if (constructor != constructors_.end()) {
                const auto& signature =
                    functions_[static_cast<std::size_t>(constructor->second)]->ir.signature;
                for (int i = 0; i < count && i + 1 < static_cast<int>(signature.parameters.size());
                    ++i)
                    emitConversion(function,
                        expr->args[static_cast<std::size_t>(i)]->inferredType,
                        signature.parameters[static_cast<std::size_t>(i + 1)],
                        expr->pos);
            }
            function.pop(count);
            emit(function, TypedIrOpcode::MakeInstance, 0, count, 0, name, expr->pos);
            function.push();
            invalidateCallableLists();
            return;
        }

        // Imported C++ reflection classes must use the same MakeInstance
        // opcode as script classes. This keeps interpreter and DirectNative
        // construction on the canonical reflection::object path and avoids
        // an unproven indirect CallValue in AOT.
        if ((named || expr->left->kind == ExprKind::Field) && expr->left->inferredType != nullptr
            && expr->left->inferredType->kind == TypeKind::Class
            && expr->left->inferredType->reflectedClass != nullptr) {
            const auto className = expr->left->inferredType->reflectedClass->name();
            function.pop(count);
            emit(function, TypedIrOpcode::MakeInstance, 0, count, 0, className, expr->pos);
            function.push();
            invalidateCallableLists();
            return;
        }
        if (name == "len") {
            if (count != 1) {
                setError(
                    ScriptDiagnosticCode::WrongArgCount, "len() expects one argument", expr->pos);
                return;
            }
            function.pop();
            emit(function, TypedIrOpcode::BuiltinLen, 0, 0, 0, {}, expr->pos);
            function.push();
            invalidateCallableLists();
            return;
        }
        if (name == "append") {
            if (count != 2) {
                setError(ScriptDiagnosticCode::WrongArgCount,
                    "append() expects a list and one value",
                    expr->pos);
                return;
            }
            function.pop(2);
            emit(function, TypedIrOpcode::BuiltinAppend, 0, 0, 0, {}, expr->pos);
            function.push();
            invalidateCallableLists();
            return;
        }
        if (name == "delete") {
            if (count != 2) {
                setError(ScriptDiagnosticCode::WrongArgCount,
                    "delete() expects a dict and one key",
                    expr->pos);
                return;
            }
            function.pop(2);
            emit(function, TypedIrOpcode::BuiltinDelete, 0, 0, 0, {}, expr->pos);
            invalidateCallableLists();
            return;
        }
        if (name == "clear") {
            if (count != 1) {
                setError(ScriptDiagnosticCode::WrongArgCount,
                    "clear() expects one container",
                    expr->pos);
                return;
            }
            function.pop();
            emit(function, TypedIrOpcode::BuiltinClear, 0, 0, 0, {}, expr->pos);
            invalidateCallableLists();
            return;
        }
        if (name == "range") {
            if (count == 1 || count == 2) {
                if ((count == 1 && expr->args[0]->kind == ExprKind::IntLit)
                    || (count == 2 && expr->args[0]->kind == ExprKind::IntLit
                        && expr->args[1]->kind == ExprKind::IntLit)) {
                    const std::int64_t start = count == 1 ? 0 : expr->args[0]->intVal;
                    const std::int64_t end =
                        count == 1 ? expr->args[0]->intVal : expr->args[1]->intVal;
                    for (int index = 0; index < count; ++index)
                        emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, expr->pos);
                    function.pop(count);
                    emit(function,
                        TypedIrOpcode::MakeRange,
                        0,
                        0,
                        (start << 32) | (end & 0xffffffffll),
                        {},
                        expr->pos);
                    function.push();
                } else {
                    function.pop(count);
                    emit(function, TypedIrOpcode::MakeRangeFromStack, 0, count, 0, {}, expr->pos);
                    function.push();
                }
                invalidateCallableLists();
                return;
            }
            setError(ScriptDiagnosticCode::WrongArgCount,
                "range() expects one or two arguments",
                expr->pos);
            return;
        }
        if (fnIt != functionIndex_.end()
            && functions_[static_cast<std::size_t>(fnIt->second)]->ir.captureCount == 0) {
            const auto& signature =
                functions_[static_cast<std::size_t>(fnIt->second)]->ir.signature;
            for (int i = 0; i < count; ++i) {
                if (i < static_cast<int>(signature.parameters.size()))
                    emitConversion(function,
                        expr->args[static_cast<std::size_t>(i)]->inferredType,
                        signature.parameters[static_cast<std::size_t>(i)],
                        expr->pos);
                else if (signature.variadic)
                    emitConversion(function,
                        expr->args[static_cast<std::size_t>(i)]->inferredType,
                        signature.variadicElement,
                        expr->pos);
            }
            function.pop(count);
            emit(function, TypedIrOpcode::Call, fnIt->second, count, 0, {}, expr->pos);
            function.push();
            invalidateCallableLists();
            return;
        }

        if (!named && expr->left->kind == ExprKind::Field && expr->left->name == "length"
            && count == 0) {
            compileExpr(function, expr->left->left);
            function.pop();
            emit(function, TypedIrOpcode::BuiltinLen, 0, 0, 0, {}, expr->pos);
            function.push();
            invalidateCallableLists();
            return;
        }

        // A script-class method has a deterministic target in the Typed IR
        // schema. Lower it as an explicit bound method value so the existing
        // MakeMethodValue -> CallValue provenance contract can classify the
        // call as Native. Reflected classes and dynamically typed receivers
        // retain the ordinary LoadField path below.
        if (!named && expr->left->kind == ExprKind::Field && expr->left->left != nullptr
            && expr->left->left->inferredType != nullptr
            && expr->left->left->inferredType->kind == TypeKind::Class
            && expr->left->left->inferredType->reflectedClass == nullptr) {
            core::pmr::String className = expr->left->left->inferredType->name;
            int methodIndex = -1;
            while (!className.empty() && methodIndex < 0) {
                bool foundClass = false;
                for (const auto& declaration : module_->decls) {
                    if (declaration->kind != StmtKind::ClassDecl
                        || declaration->className != className)
                        continue;
                    foundClass = true;
                    for (const auto& method : declaration->methods) {
                        if (method->funcName == expr->left->name) {
                            const auto methodIt = functionIndex_.find(
                                (declaration->className + "." + method->funcName).c_str());
                            if (methodIt != functionIndex_.end())
                                methodIndex = methodIt->second;
                            break;
                        }
                    }
                    if (methodIndex < 0)
                        className = declaration->baseClassName;
                    break;
                }
                if (!foundClass)
                    break;
            }
            if (methodIndex >= 0) {
                compileExpr(function, expr->left->left);
                function.pop();
                emit(function,
                    TypedIrOpcode::MakeMethodValue,
                    methodIndex,
                    0,
                    0,
                    expr->left->name,
                    expr->left->pos);
                function.push();
                function.pop(count + 1);
                emit(function, TypedIrOpcode::CallValue, 0, count, 0, {}, expr->pos);
                function.operations.back().closureTargetFunctionId =
                    functions_[static_cast<std::size_t>(methodIndex)]->ir.id;
                function.operations.back().closureCaptureCount =
                    kScriptMethodProvenanceCaptureCount;
                function.operations.back().text = expr->left->name;
                function.push();
                invalidateCallableLists();
                return;
            }
        }
        core::pmr::String callableBindingId{&core::defaultCpuMemoryResource()};
        if (named) {
            if (findLocal(function, name) != nullptr
                || std::any_of(function.captures.begin(),
                    function.captures.end(),
                    [&name](const DirectCapture& capture) {
                        return capture.name == name;
                    })
                || globals_.find(name.c_str()) != globals_.end()) {
                callableBindingId = bindingForExpr(function, expr->left);
                compileExpr(function, expr->left);
            } else {
                setError(ScriptDiagnosticCode::UndeclaredName, "undeclared function", expr->pos);
                return;
            }
        } else {
            callableBindingId = bindingForExpr(function, expr->left);
            if (callableBindingId.empty())
                callableBindingId = syntacticBindingForCall(function, expr->left, count);
            compileExpr(function, expr->left);
            if (!callableBindingId.empty() && !function.operations.empty()
                && (function.operations.back().opcode == TypedIrOpcode::LoadField
                    || function.operations.back().opcode == TypedIrOpcode::LoadModuleExport))
                function.operations.back().callableBindingId = callableBindingId;
        }
        const auto callableTargetId = syntacticTargetForCall(function, expr->left);
        function.pop(count + 1);
        emit(function, TypedIrOpcode::CallValue, 0, count, 0, {}, expr->pos);
        function.operations.back().callableBindingId = std::move(callableBindingId);
        function.operations.back().targetId = callableTargetId;
        if (function.operations.back().callableBindingId.empty() && !callableTargetId.empty()) {
            int localTargetIndex = -1;
            const auto localTarget = functionIndex_.find(callableTargetId.c_str());
            if (localTarget != functionIndex_.end())
                localTargetIndex = localTarget->second;
            const auto separator = callableTargetId.rfind('.');
            if (localTargetIndex < 0 && separator != core::pmr::String::npos) {
                core::pmr::Set<core::pmr::String> visited{&core::defaultCpuMemoryResource()};
                const auto methodName = callableTargetId.substr(separator + 1u);
                const auto resolveInherited = [&](auto&& self,
                                                  const core::pmr::String& className) -> int {
                    if (!visited.emplace(className).second)
                        return -1;
                    for (const auto& declaration : module_->decls) {
                        if (declaration->kind != StmtKind::ClassDecl
                            || declaration->className != className)
                            continue;
                        const auto method =
                            functionIndex_.find((className + "." + methodName).c_str());
                        if (method != functionIndex_.end())
                            return method->second;
                        return declaration->baseClassName.empty()
                            ? -1
                            : self(self, declaration->baseClassName);
                    }
                    return -1;
                };
                localTargetIndex =
                    resolveInherited(resolveInherited, callableTargetId.substr(0u, separator));
            }
            if (localTargetIndex >= 0 && localTargetIndex < static_cast<int>(functions_.size())) {
                function.operations.back().closureTargetFunctionId =
                    functions_[static_cast<std::size_t>(localTargetIndex)]->ir.id;
                function.operations.back().closureCaptureCount =
                    kScriptMethodProvenanceCaptureCount;
            }
        }
        function.push();
        invalidateCallableLists();
    }

    void compileStmt(DirectFunction& function, const StmtPtr& stmt)
    {
        if (!stmt || !ok())
            return;
        switch (stmt->kind) {
        case StmtKind::VarDecl: {
            const bool global = function.moduleInit;
            const int slot = global ? ensureGlobal(stmt->varName)
                                    : function.alloc(stmt->varName, false, stmt->varType);
            DirectLocal* local = global ? nullptr : findLocal(function, stmt->varName);
            const auto callableBinding =
                stmt->expr ? bindingForExpr(function, stmt->expr) : core::pmr::String{};
            std::vector<core::pmr::String> callableListBindings;
            const bool hasCallableListBinding =
                stmt->expr && listBindingForExpr(function, stmt->expr, callableListBindings);
            CallableDictBindings callableDictBindings{&core::defaultCpuMemoryResource()};
            const bool hasCallableDictBinding =
                stmt->expr && dictBindingForExpr(function, stmt->expr, callableDictBindings);
            if (stmt->expr)
                compileExpr(function, stmt->expr);
            else {
                emit(function, TypedIrOpcode::LoadNil, 0, 0, 0, {}, stmt->pos);
                function.push();
            }
            emitConversion(function,
                stmt->expr ? stmt->expr->inferredType : TypePtr{},
                stmt->varType,
                stmt->pos);
            function.pop();
            emit(function,
                global ? TypedIrOpcode::StoreGlobal : TypedIrOpcode::StoreLocal,
                slot,
                0,
                0,
                {},
                stmt->pos);
            if (global) {
                setGlobalBinding(stmt->varName, callableBinding);
                if (hasCallableListBinding)
                    setGlobalListBinding(stmt->varName, callableListBindings);
                if (hasCallableDictBinding)
                    setGlobalDictBinding(stmt->varName, callableDictBindings);
            } else if (hasCallableListBinding)
                setListBinding(local, callableListBindings);
            else if (hasCallableDictBinding)
                setDictBinding(local, callableDictBindings);
            else
                setBinding(local, callableBinding);
            break;
        }
        case StmtKind::Assign: {
            if (stmt->target->kind == ExprKind::Ident) {
                const auto callableBinding = stmt->assignOp == "="
                    ? bindingForExpr(function, stmt->value)
                    : core::pmr::String{};
                std::vector<core::pmr::String> callableListBindings;
                const bool hasCallableListBinding = stmt->assignOp == "="
                    && listBindingForExpr(function, stmt->value, callableListBindings);
                CallableDictBindings callableDictBindings{&core::defaultCpuMemoryResource()};
                const bool hasCallableDictBinding = stmt->assignOp == "="
                    && dictBindingForExpr(function, stmt->value, callableDictBindings);
                compileExpr(function, stmt->value);
                DirectLocal* local = findLocal(function, stmt->target->name);
                int capture = -1;
                for (const auto& item : function.captures)
                    if (item.name == stmt->target->name)
                        capture = item.slot;
                auto global = globals_.find(stmt->target->name.c_str());
                bool localFastPath = false;
                const bool intLiteral = stmt->value->kind == ExprKind::IntLit;
                const bool int64Literal = stmt->value->kind == ExprKind::Int64Lit;
                const bool intPath = local != nullptr && !local->captured
                    && directIntType(local->type) && intLiteral
                    && stmt->value->intVal >= std::numeric_limits<int>::min()
                    && stmt->value->intVal <= std::numeric_limits<int>::max();
                const bool int64Path = local != nullptr && !local->captured
                    && directInt64Type(local->type) && (intLiteral || int64Literal);
                const bool floatLiteral = stmt->value->kind == ExprKind::FloatLit || intLiteral;
                const bool floatPath = local != nullptr && !local->captured
                    && directFloatType(local->type) && floatLiteral;
                if ((stmt->assignOp == "+=" || stmt->assignOp == "-=") && floatPath) {
                    std::int64_t bits = 0;
                    const double value = stmt->value->kind == ExprKind::FloatLit
                        ? stmt->value->floatVal
                        : static_cast<double>(stmt->value->intVal);
                    std::memcpy(&bits, &value, sizeof(bits));
                    emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, stmt->pos);
                    function.pop();
                    emit(function,
                        stmt->assignOp == "+=" ? TypedIrOpcode::LocalAddFloatConst
                                               : TypedIrOpcode::LocalSubFloatConst,
                        local->slot,
                        0,
                        bits,
                        {},
                        stmt->pos);
                    localFastPath = true;
                } else if (stmt->assignOp == "+=" && (intPath || int64Path)) {
                    emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, stmt->pos);
                    function.pop();
                    emit(function,
                        intPath ? TypedIrOpcode::LocalAddIntConst
                                : TypedIrOpcode::LocalAddInt64Const,
                        local->slot,
                        0,
                        stmt->value->intVal,
                        {},
                        stmt->pos);
                    localFastPath = true;
                } else if (stmt->assignOp == "-=" && (intPath || int64Path)) {
                    emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, stmt->pos);
                    function.pop();
                    emit(function,
                        intPath ? TypedIrOpcode::LocalSubIntConst
                                : TypedIrOpcode::LocalSubInt64Const,
                        local->slot,
                        0,
                        stmt->value->intVal,
                        {},
                        stmt->pos);
                    localFastPath = true;
                } else if (stmt->assignOp != "=") {
                    emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, stmt->pos);
                    function.pop();
                    compileExpr(function, stmt->target);
                    compileExpr(function, stmt->value);
                    function.pop(2);
                    TypedIrOpcode opcode = stmt->assignOp[0] == '+' ? TypedIrOpcode::Add
                        : stmt->assignOp[0] == '-'                  ? TypedIrOpcode::Sub
                        : stmt->assignOp[0] == '*'                  ? TypedIrOpcode::Mul
                        : stmt->assignOp[0] == '/'                  ? TypedIrOpcode::Div
                                                                    : TypedIrOpcode::Mod;
                    emit(function, opcode, 0, 0, 0, {}, stmt->pos);
                    function.push();
                }
                if (localFastPath) {
                    clearBinding(local);
                    break;
                }
                emitConversion(
                    function, stmt->value->inferredType, stmt->target->inferredType, stmt->pos);
                function.pop();
                if (local) {
                    if (local->captured) {
                        emit(function, TypedIrOpcode::AddrLocal, local->slot, 0, 0, {}, stmt->pos);
                        emit(function, TypedIrOpcode::StoreIndirect, 0, 0, 0, {}, stmt->pos);
                    } else
                        emit(function, TypedIrOpcode::StoreLocal, local->slot, 0, 0, {}, stmt->pos);
                } else if (capture >= 0)
                    emit(function, TypedIrOpcode::StoreCapture, capture, 0, 0, {}, stmt->pos);
                else if (global != globals_.end())
                    emit(function, TypedIrOpcode::StoreGlobal, global->second, 0, 0, {}, stmt->pos);
                else
                    setError(ScriptDiagnosticCode::UndeclaredName,
                        "undeclared assignment target",
                        stmt->pos);
                if (local) {
                    clearBinding(local);
                    if (hasCallableListBinding)
                        setListBinding(local, callableListBindings);
                    else if (hasCallableDictBinding)
                        setDictBinding(local, callableDictBindings);
                    else if (stmt->assignOp == "=")
                        setBinding(local, callableBinding);
                } else if (capture >= 0) {
                    for (auto& item : function.captures) {
                        if (item.slot == capture) {
                            item.callableBindingId.clear();
                            if (item.source != nullptr)
                                clearBinding(item.source);
                            break;
                        }
                    }
                } else if (global != globals_.end()) {
                    setGlobalBinding(stmt->target->name,
                        stmt->assignOp == "=" ? callableBinding : core::pmr::String{});
                    if (stmt->assignOp == "=" && hasCallableListBinding)
                        setGlobalListBinding(stmt->target->name, callableListBindings);
                    if (stmt->assignOp == "=" && hasCallableDictBinding)
                        setGlobalDictBinding(stmt->target->name, callableDictBindings);
                }
            } else if (stmt->target->kind == ExprKind::Index) {
                compileExpr(function, stmt->target->left);
                compileExpr(function, stmt->target->right);
                compileExpr(function, stmt->value);
                emitConversion(
                    function, stmt->value->inferredType, stmt->target->inferredType, stmt->pos);
                function.pop(3);
                emit(function, TypedIrOpcode::StoreIndex, 0, 0, 0, {}, stmt->pos);
                if (stmt->target->left != nullptr && stmt->target->left->kind == ExprKind::Ident)
                    clearBindingTarget(function, stmt->target->left->name);
            } else if (stmt->target->kind == ExprKind::Field) {
                if (stmt->assignOp == "=") {
                    compileExpr(function, stmt->target->left);
                    compileExpr(function, stmt->value);
                    emitConversion(
                        function, stmt->value->inferredType, stmt->target->inferredType, stmt->pos);
                    function.pop(2);
                    emit(function,
                        TypedIrOpcode::StoreField,
                        0,
                        0,
                        0,
                        stmt->target->name,
                        stmt->pos);
                } else {
                    // Store the receiver and result so a compound property write
                    // evaluates the receiver exactly once and preserves StoreField's
                    // base-then-value stack order.
                    const int receiverSlot =
                        function.allocTemporary(stmt->target->left->inferredType);
                    const int resultSlot = function.allocTemporary(stmt->target->inferredType);
                    compileExpr(function, stmt->target->left);
                    function.pop();
                    emit(function, TypedIrOpcode::StoreLocal, receiverSlot, 0, 0, {}, stmt->pos);
                    emit(function, TypedIrOpcode::LoadLocal, receiverSlot, 0, 0, {}, stmt->pos);
                    function.push();
                    function.pop();
                    emit(
                        function, TypedIrOpcode::LoadField, 0, 0, 0, stmt->target->name, stmt->pos);
                    function.push();
                    compileExpr(function, stmt->value);
                    function.pop(2);
                    const TypedIrOpcode opcode = stmt->assignOp[0] == '+' ? TypedIrOpcode::Add
                        : stmt->assignOp[0] == '-'                        ? TypedIrOpcode::Sub
                        : stmt->assignOp[0] == '*'                        ? TypedIrOpcode::Mul
                        : stmt->assignOp[0] == '/'                        ? TypedIrOpcode::Div
                                                                          : TypedIrOpcode::Mod;
                    emit(function, opcode, 0, 0, 0, {}, stmt->pos);
                    function.push();
                    emitConversion(function,
                        stmt->target->inferredType,
                        stmt->target->inferredType,
                        stmt->pos);
                    function.pop();
                    emit(function, TypedIrOpcode::StoreLocal, resultSlot, 0, 0, {}, stmt->pos);
                    emit(function, TypedIrOpcode::LoadLocal, receiverSlot, 0, 0, {}, stmt->pos);
                    function.push();
                    emit(function, TypedIrOpcode::LoadLocal, resultSlot, 0, 0, {}, stmt->pos);
                    function.push();
                    function.pop(2);
                    emit(function,
                        TypedIrOpcode::StoreField,
                        0,
                        0,
                        0,
                        stmt->target->name,
                        stmt->pos);
                }
            }
            break;
        }
        case StmtKind::Expr: {
            const auto before = function.currentStack;
            compileExpr(function, stmt->expr);
            if (function.currentStack > before) {
                emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, stmt->pos);
                function.pop();
            }
            break;
        }
        case StmtKind::Return:
            if (stmt->expr) {
                compileExpr(function, stmt->expr);
                emitConversion(function, stmt->expr->inferredType, function.returnType, stmt->pos);
                function.pop();
                emit(function,
                    directIntType(function.returnType) ? TypedIrOpcode::ReturnInt
                                                       : TypedIrOpcode::Return,
                    1,
                    0,
                    0,
                    {},
                    stmt->pos);
            } else
                emit(function, TypedIrOpcode::Return, 0, 0, 0, {}, stmt->pos);
            break;
        case StmtKind::If: {
            const BindingSnapshot baseline = snapshotBindings(function);
            std::vector<BindingSnapshot> paths;
            const int falseJump = conditionJump(function, stmt->cond);
            compileBlock(function, stmt->body);
            paths.push_back(snapshotBindings(function));
            std::vector<int> endJumps;
            if (!stmt->elseBody.empty() || !stmt->elifConds.empty())
                endJumps.push_back(emitJump(function, TypedIrOpcode::Jump, stmt->pos));
            patch(function, falseJump, static_cast<int>(function.operations.size()));
            for (std::size_t i = 0; i < stmt->elifConds.size(); ++i) {
                restoreBindings(baseline);
                const int next = conditionJump(function, stmt->elifConds[i]);
                compileBlock(function, stmt->elifBodies[i]);
                paths.push_back(snapshotBindings(function));
                if (i + 1 < stmt->elifConds.size() || !stmt->elseBody.empty()) {
                    endJumps.push_back(emitJump(function, TypedIrOpcode::Jump, stmt->pos));
                    patch(function, next, static_cast<int>(function.operations.size()));
                } else {
                    patch(function, next, static_cast<int>(function.operations.size()));
                }
            }
            if (!stmt->elseBody.empty()) {
                restoreBindings(baseline);
                compileBlock(function, stmt->elseBody);
                paths.push_back(snapshotBindings(function));
            } else {
                // The condition can be false without entering any branch.
                paths.push_back(baseline);
            }
            const int end = static_cast<int>(function.operations.size());
            for (const int jump : endJumps)
                patch(function, jump, end);
            mergeBindingSnapshots(function, baseline, paths);
            break;
        }
        case StmtKind::While: {
            const BindingSnapshot baseline = snapshotBindings(function);
            const int start = static_cast<int>(function.operations.size());
            const int exit = conditionJump(function, stmt->cond);
            const auto oldBreak = function.breakJumps;
            const auto oldContinue = function.continueJumps;
            function.breakJumps.clear();
            function.continueJumps.clear();
            compileBlock(function, stmt->body);
            const BindingSnapshot bodyState = snapshotBindings(function);
            emit(function, TypedIrOpcode::Jump, start);
            const int after = static_cast<int>(function.operations.size());
            patch(function, exit, after);
            for (int jump : function.breakJumps)
                patch(function, jump, after);
            for (int jump : function.continueJumps)
                patch(function, jump, start);
            function.breakJumps = oldBreak;
            function.continueJumps = oldContinue;
            mergeBindingSnapshots(
                function, baseline, std::vector<BindingSnapshot>{baseline, bodyState});
            break;
        }
        case StmtKind::For: {
            const int rangeSlot = function.allocTemporary();
            const int indexSlot = function.allocTemporary();
            const int valueSlot = function.alloc(stmt->iterName, false, stmt->iterType);
            int keySlot = -1;
            if (!stmt->iterKeyName.empty())
                keySlot = function.alloc(stmt->iterKeyName, false, stmt->keyType);
            compileExpr(function, stmt->iterable);
            function.pop();
            emit(function, TypedIrOpcode::RangeInit, rangeSlot, indexSlot, 0, {}, stmt->pos);
            const int loop = static_cast<int>(function.operations.size());
            emit(function,
                TypedIrOpcode::RangeNext,
                rangeSlot,
                indexSlot,
                packTypedIrRangeTargets(keySlot >= 0 ? keySlot : valueSlot, valueSlot),
                {},
                stmt->pos);
            function.push();
            const int exit = emitJump(function, TypedIrOpcode::JumpIfFalse, stmt->pos);
            function.pop();
            const BindingSnapshot baseline = snapshotBindings(function);
            const auto oldBreak = function.breakJumps;
            const auto oldContinue = function.continueJumps;
            function.breakJumps.clear();
            function.continueJumps.clear();
            compileBlock(function, stmt->body);
            const BindingSnapshot bodyState = snapshotBindings(function);
            emit(function, TypedIrOpcode::Jump, loop);
            const int after = static_cast<int>(function.operations.size());
            patch(function, exit, after);
            for (int jump : function.breakJumps)
                patch(function, jump, after);
            for (int jump : function.continueJumps)
                patch(function, jump, loop);
            function.breakJumps = oldBreak;
            function.continueJumps = oldContinue;
            mergeBindingSnapshots(
                function, baseline, std::vector<BindingSnapshot>{baseline, bodyState});
            break;
        }
        case StmtKind::Break:
            function.breakJumps.push_back(emitJump(function, TypedIrOpcode::Jump, stmt->pos));
            break;
        case StmtKind::Continue:
            function.continueJumps.push_back(emitJump(function, TypedIrOpcode::Jump, stmt->pos));
            break;
        case StmtKind::Pass:
            break;
        case StmtKind::Try: {
            if (stmt->exceptClauses.empty()) {
                setError(ScriptDiagnosticCode::UnexpectedToken,
                    "try requires an except clause",
                    stmt->pos);
                return;
            }
            if (stmt->exceptClauses.size() == 1u) {
                const auto& clause = stmt->exceptClauses.front();
                const int push = static_cast<int>(function.operations.size());
                emit(function, TypedIrOpcode::PushHandler, 0, 0, 0, clause.errorName, stmt->pos);
                compileBlock(function, stmt->tryBody);
                emit(function, TypedIrOpcode::PopHandler, 0, 0, 0, {}, stmt->pos);
                const int end = emitJump(function, TypedIrOpcode::Jump, stmt->pos);
                const int handler = static_cast<int>(function.operations.size());
                patch(function, push, handler);
                if (!clause.varName.empty()) {
                    const int slot = function.alloc(clause.varName, false, {});
                    emit(function, TypedIrOpcode::StoreLocal, slot, 0, 0, {}, stmt->pos);
                } else
                    emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, stmt->pos);
                compileBlock(function, clause.body);
                patch(function, end, static_cast<int>(function.operations.size()));
                clearBindingsWritten(function, stmt->tryBody);
                clearBindingsWritten(function, clause.body);
                break;
            }
            core::pmr::Vector<int> pushPcs{&core::defaultCpuMemoryResource()};
            pushPcs.reserve(stmt->exceptClauses.size());
            for (std::size_t index = stmt->exceptClauses.size(); index-- > 0;) {
                const auto& clause = stmt->exceptClauses[index];
                pushPcs.push_back(static_cast<int>(function.operations.size()));
                emit(function, TypedIrOpcode::PushHandler, 0, 0, 0, clause.errorName, stmt->pos);
            }
            compileBlock(function, stmt->tryBody);
            for (std::size_t index = 0; index < stmt->exceptClauses.size(); ++index)
                emit(function, TypedIrOpcode::PopHandler, 0, 0, 0, {}, stmt->pos);
            const int end = emitJump(function, TypedIrOpcode::Jump, stmt->pos);
            core::pmr::Vector<int> clauseEndJumps{&core::defaultCpuMemoryResource()};
            clauseEndJumps.reserve(stmt->exceptClauses.size());
            for (std::size_t index = 0; index < stmt->exceptClauses.size(); ++index) {
                const auto& clause = stmt->exceptClauses[index];
                const int handler = static_cast<int>(function.operations.size());
                patch(function, pushPcs[stmt->exceptClauses.size() - 1u - index], handler);
                for (std::size_t sibling = index + 1u; sibling < stmt->exceptClauses.size();
                    ++sibling)
                    // Exception dispatch already removed the matching
                    // handler; this is sibling cleanup on the handler path.
                    emit(function, TypedIrOpcode::PopHandler, -1, 0, 0, {}, stmt->pos);
                if (!clause.varName.empty()) {
                    const int slot = function.alloc(clause.varName, false, {});
                    emit(function, TypedIrOpcode::StoreLocal, slot, 0, 0, {}, stmt->pos);
                } else
                    emit(function, TypedIrOpcode::Pop, 0, 0, 0, {}, stmt->pos);
                compileBlock(function, clause.body);
                clauseEndJumps.push_back(emitJump(function, TypedIrOpcode::Jump, stmt->pos));
            }
            const int after = static_cast<int>(function.operations.size());
            patch(function, end, after);
            for (const int jump : clauseEndJumps)
                patch(function, jump, after);
            clearBindingsWritten(function, stmt->tryBody);
            for (const auto& clause : stmt->exceptClauses)
                clearBindingsWritten(function, clause.body);
            break;
        }
        case StmtKind::Raise:
            if (stmt->expr)
                compileExpr(function, stmt->expr);
            else {
                emit(function, TypedIrOpcode::LoadNil, 0, 0, 0, {}, stmt->pos);
                function.push();
            }
            function.pop();
            emit(function, TypedIrOpcode::Throw, 0, 0, 0, {}, stmt->pos);
            break;
        case StmtKind::Yield: {
            if (stmt->yieldFrom) {
                function.ir.hasYieldFrom = true;
                // Keep Typed-IR and legacy bytecode lowering identical. This
                // The runtime records this deterministic shape so bounded
                // send/throw/close can forward to a resumable child.
                const int rangeSlot = function.allocTemporary();
                const int indexSlot = function.allocTemporary();
                const int valueSlot = function.allocTemporary();
                compileExpr(function, stmt->expr);
                const int sourcePc = static_cast<int>(function.operations.size()) - 1;
                function.pop();
                const int initPc = emit(
                    function, TypedIrOpcode::RangeInit, rangeSlot, indexSlot, 0, {}, stmt->pos);
                const int loop = static_cast<int>(function.operations.size());
                const int rangeNext = emit(function,
                    TypedIrOpcode::RangeNext,
                    rangeSlot,
                    indexSlot,
                    packTypedIrRangeTargets(valueSlot, valueSlot),
                    {},
                    stmt->pos);
                function.operations[static_cast<std::size_t>(rangeNext)].delegatedGeneratorRange =
                    true;
                function.push();
                const int exit = emitJump(function, TypedIrOpcode::JumpIfFalse, stmt->pos);
                function.pop();
                const int load =
                    emit(function, TypedIrOpcode::LoadLocal, valueSlot, 0, 0, {}, stmt->pos);
                function.push();
                const int yield = emit(function, TypedIrOpcode::Yield, 0, 0, 0, {}, stmt->pos);
                function.pop();
                const int backEdge = emit(function, TypedIrOpcode::Jump, loop, 0, 0, {}, stmt->pos);
                const int exitPc = static_cast<int>(function.operations.size());
                patch(function, exit, exitPc);
                function.ir.delegationSites.push_back(TypedIrDelegationSite{sourcePc,
                    initPc,
                    rangeNext,
                    exit,
                    load,
                    yield,
                    backEdge,
                    exitPc,
                    rangeSlot,
                    indexSlot,
                    valueSlot});
                clearAllCallableContainerBindings();
                break;
            }
            if (stmt->expr)
                compileExpr(function, stmt->expr);
            else {
                emit(function, TypedIrOpcode::LoadNil, 0, 0, 0, {}, stmt->pos);
                function.push();
            }
            emit(function, TypedIrOpcode::Yield, 0, 0, 0, {}, stmt->pos);
            function.pop();
            clearAllCallableContainerBindings();
            break;
        }
        case StmtKind::FuncDecl: {
            const auto captures = capturesFor(function, stmt->body);
            int index = static_cast<int>(functions_.size());
            addFunction(stmt->funcName, stmt);
            compileFunction(stmt, index, captures);
            for (const auto& capture : captures) {
                if (auto* local = findLocal(function, capture.name)) {
                    emit(function, TypedIrOpcode::AddrLocal, local->slot, 0, 0, {}, stmt->pos);
                    function.push();
                }
            }
            if (!captures.empty()) {
                emit(function,
                    TypedIrOpcode::MakeClosure,
                    index,
                    static_cast<int>(captures.size()),
                    0,
                    {},
                    stmt->pos);
                function.pop(static_cast<int>(captures.size()));
            } else
                emit(function, TypedIrOpcode::MakeFuncValue, index, 0, 0, {}, stmt->pos);
            function.push();
            const int slot = function.alloc(stmt->funcName, false, {});
            function.pop();
            emit(function, TypedIrOpcode::StoreLocal, slot, 0, 0, {}, stmt->pos);
            break;
        }
        case StmtKind::ImportDecl:
            if (stmt->importIsModuleImport) {
                const auto& binding =
                    stmt->importAlias.empty() ? stmt->importModule : stmt->importAlias;
                emit(function, TypedIrOpcode::LoadModule, 0, 0, 0, stmt->importModule, stmt->pos);
                function.push();
                emit(function,
                    TypedIrOpcode::StoreGlobal,
                    ensureGlobal(binding),
                    0,
                    0,
                    {},
                    stmt->pos);
                function.pop();
                clearGlobalBinding(binding);
            } else
                for (const auto& item : stmt->importNames) {
                    const auto& binding = item.alias.empty() ? item.name : item.alias;
                    emit(function,
                        TypedIrOpcode::LoadModule,
                        0,
                        0,
                        0,
                        stmt->importModule,
                        stmt->pos);
                    function.push();
                    emit(function, TypedIrOpcode::LoadModuleExport, 0, 0, 0, item.name, stmt->pos);
                    const auto imported = globalCallableBindings_.find(binding.c_str());
                    if (imported != globalCallableBindings_.end())
                        function.operations.back().callableBindingId = imported->second;
                    emit(function,
                        TypedIrOpcode::StoreGlobal,
                        ensureGlobal(binding),
                        0,
                        0,
                        {},
                        stmt->pos);
                    function.pop();
                    setGlobalBinding(binding,
                        imported == globalCallableBindings_.end() ? core::pmr::String{}
                                                                  : imported->second);
                }
            break;
        case StmtKind::ClassDecl:
            break;
        }
    }

    void finalize(DirectFunction& function)
    {
        for (auto& op : function.operations) {
            if ((op.opcode == TypedIrOpcode::Call || op.opcode == TypedIrOpcode::MakeFuncValue
                    || op.opcode == TypedIrOpcode::MakeClosure
                    || op.opcode == TypedIrOpcode::MakeMethodValue)
                && op.a >= 0 && op.a < static_cast<int>(functions_.size()))
                op.targetId = functions_[static_cast<std::size_t>(op.a)]->ir.id;
            else if (op.opcode == TypedIrOpcode::LoadModule)
                appendUnique(dependencies_, op.text);
            else if (op.opcode == TypedIrOpcode::MakeInstance)
                appendUnique(reflectionDependencies_, op.text);
        }
        std::vector<int> boundaries{0};
        for (std::size_t pc = 0; pc < function.operations.size(); ++pc) {
            const auto& op = function.operations[pc];
            if (op.opcode == TypedIrOpcode::Jump || directConditional(op.opcode)) {
                if (op.a >= 0 && op.a < static_cast<int>(function.operations.size()))
                    boundaries.push_back(op.a);
                if (pc + 1 < function.operations.size())
                    boundaries.push_back(static_cast<int>(pc + 1));
            } else if (directTerminator(op.opcode) && pc + 1 < function.operations.size())
                boundaries.push_back(static_cast<int>(pc + 1));
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        std::unordered_map<int, int> pcToBlock;
        for (std::size_t i = 0; i < boundaries.size(); ++i) {
            const int end = i + 1 < boundaries.size()
                ? boundaries[i + 1]
                : static_cast<int>(function.operations.size());
            for (int pc = boundaries[i]; pc < end; ++pc)
                pcToBlock[pc] = static_cast<int>(i);
        }
        auto blockId = [&boundaries](int index) {
            core::pmr::String id("b", &core::defaultCpuMemoryResource());
            id += std::to_string(boundaries[static_cast<std::size_t>(index)]);
            return id;
        };
        for (std::size_t i = 0; i < boundaries.size(); ++i) {
            const int begin = boundaries[i];
            const int end = i + 1 < boundaries.size()
                ? boundaries[i + 1]
                : static_cast<int>(function.operations.size());
            TypedIrBasicBlock block;
            block.id = blockId(static_cast<int>(i));
            for (int pc = begin; pc < end; ++pc)
                block.operations.push_back(function.operations[static_cast<std::size_t>(pc)]);
            if (end > begin) {
                const auto& last = function.operations[static_cast<std::size_t>(end - 1)];
                auto add = [&block, &pcToBlock, &blockId](int target) {
                    auto it = pcToBlock.find(target);
                    if (it != pcToBlock.end())
                        block.successors.push_back(blockId(it->second));
                };
                if (last.opcode == TypedIrOpcode::Jump)
                    add(last.a);
                else if (directConditional(last.opcode)) {
                    add(last.a);
                    add(end);
                } else if (!directTerminator(last.opcode))
                    add(end);
            }
            function.ir.blocks.push_back(std::move(block));
        }
        std::vector<std::size_t> handlerStack;
        int operationIndex = 0;
        for (const auto& op : function.operations) {
            if (op.opcode == TypedIrOpcode::Yield || op.opcode == TypedIrOpcode::Await) {
                if (op.source.valid()) {
                    TypedIrSafepoint point;
                    point.id = "sp:" + std::to_string(operationIndex);
                    point.operationIndex = operationIndex;
                    point.source = op.source;
                    auto block = pcToBlock.find(operationIndex);
                    if (block != pcToBlock.end()) {
                        point.resumeLabel = blockId(block->second);
                        point.resumeLabel += ":";
                        point.resumeLabel += std::to_string(
                            operationIndex - boundaries[static_cast<std::size_t>(block->second)]);
                    }
                    for (int slot = 0; slot < function.ir.localCount; ++slot)
                        point.canonicalSlots.push_back(slot);
                    function.ir.safepoints.push_back(std::move(point));
                }
            }
            if (op.opcode == TypedIrOpcode::PushHandler) {
                TypedIrExceptionRegion region;
                region.id = "handler:" + std::to_string(operationIndex);
                region.startOperation = operationIndex + 1;
                region.handlerOperation = op.a;
                auto block = pcToBlock.find(op.a);
                region.handlerBlockId = block == pcToBlock.end() ? "" : blockId(block->second);
                region.source = op.source;
                handlerStack.push_back(function.ir.exceptionRegions.size());
                function.ir.exceptionRegions.push_back(std::move(region));
            } else if (op.opcode == TypedIrOpcode::PopHandler && !handlerStack.empty()) {
                function.ir.exceptionRegions[handlerStack.back()].endOperation = operationIndex;
                handlerStack.pop_back();
            }
            ++operationIndex;
        }
        if (!handlerStack.empty())
            setError(ScriptDiagnosticCode::TypedIrInvalid,
                "typed IR exception handler stack is not balanced",
                {});
    }

    static void appendUnique(std::vector<core::pmr::String>& values, const core::pmr::String& value)
    {
        if (std::find(values.begin(), values.end(), value) == values.end())
            values.push_back(value);
    }
};

class CallableBindingCollector final {
public:
    static bool collect(
        ScriptTypedModule& module, ModuleRegistry* registry, ScriptDiagnostic& error)
    {
        if (registry == nullptr)
            return true;
        for (const auto& declaration : module.syntax->decls) {
            if (declaration->kind != StmtKind::ImportDecl)
                continue;
            if (!appendReflectionModule(module, *registry, declaration->importModule, error))
                return false;
            if (declaration->importIsModuleImport) {
                for (const auto& exportName : registry->queryExports(declaration->importModule)) {
                    if (!appendExport(
                            module, *registry, declaration->importModule, exportName, error))
                        return false;
                }
            } else {
                for (const auto& item : declaration->importNames) {
                    if (!appendExport(
                            module, *registry, declaration->importModule, item.name, error))
                        return false;
                }
            }
        }
        return true;
    }

private:
    static void setConflict(ScriptDiagnostic& error)
    {
        error = ScriptDiagnostic{ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String(
                "imported callable binding contract conflicts", &core::defaultCpuMemoryResource())};
    }

    static bool append(
        ScriptTypedModule& module, TypedIrCallableBinding binding, ScriptDiagnostic& error)
    {
        if (appendCallableBinding(module.callableBindings, binding))
            return true;
        setConflict(error);
        return false;
    }

    static bool appendReflection(ScriptTypedModule& module,
        const ReflectionCallableContract& contract,
        ScriptDiagnostic& error)
    {
        TypedIrCallableBinding binding;
        binding.bindingId = contract.bindingId;
        binding.origin = TypedIrCallableOrigin::ReflectionMember;
        binding.targetId = contract.targetId;
        binding.signatureHash = contract.signatureHash;
        binding.fixedArity = contract.fixedArity;
        binding.maySuspend = contract.maySuspend;
        binding.lifetimeVersion = contract.lifetimeVersion;
        return append(module, std::move(binding), error);
    }

    static bool appendScript(
        ScriptTypedModule& module, const ScriptFunctionContract& contract, ScriptDiagnostic& error)
    {
        if (!contract.nativeBindingValid()
            || contract.fixedArity != static_cast<int>(contract.params.size()))
            return true;
        TypedIrCallableBinding binding;
        binding.bindingId = contract.bindingId;
        binding.origin = TypedIrCallableOrigin::ScriptFunction;
        binding.targetId = contract.targetId;
        binding.signatureHash = contract.signatureHash;
        binding.fixedArity = contract.fixedArity;
        binding.maySuspend = contract.maySuspend;
        binding.generatorBridge = contract.generatorBridge;
        binding.lifetimeVersion = contract.lifetimeVersion;
        return append(module, std::move(binding), error);
    }

    static bool appendHost(ScriptTypedModule& module,
        const core::pmr::String& moduleName,
        const core::pmr::String& exportName,
        const HostCallableContract& contract,
        ScriptDiagnostic& error)
    {
        TypedIrCallableBinding binding;
        binding.bindingId = contract.bindingId;
        binding.origin = TypedIrCallableOrigin::HostCapability;
        binding.targetId = moduleName + "." + exportName;
        binding.signatureHash = contract.signatureHash;
        binding.fixedArity = contract.fixedArity;
        binding.maySuspend = contract.maySuspend;
        binding.lifetimeVersion = contract.lifetimeVersion;
        return append(module, std::move(binding), error);
    }

    static bool appendReflectionModule(ScriptTypedModule& module,
        ModuleRegistry& registry,
        const core::pmr::String& moduleName,
        ScriptDiagnostic& error)
    {
        for (const auto& reflection : registry.queryReflectionCallableContracts(moduleName)) {
            if (!appendReflection(module, reflection, error))
                return false;
        }
        return true;
    }

    static bool appendExport(ScriptTypedModule& module,
        ModuleRegistry& registry,
        const core::pmr::String& moduleName,
        const core::pmr::String& exportName,
        ScriptDiagnostic& error)
    {
        if (const auto* script = registry.findScriptFunction(moduleName, exportName)) {
            if (script->nativeBindingValid())
                return appendScript(module, *script, error);
        }
        if (const auto* host = registry.findHostCallableContract(moduleName, exportName))
            return appendHost(module, moduleName, exportName, *host, error);
        if (const auto* reflection =
                registry.findReflectionCallableContract(moduleName, exportName))
            return appendReflection(module, *reflection, error);
        return true;
    }
};

} // namespace

ScriptFrontendResult ScriptFrontend::compile(core::pmr::String source,
    core::pmr::String filename,
    core::pmr::String moduleId,
    ModuleRegistry* moduleRegistry)
{
    ScriptFrontendResult result;
    if (moduleId.empty()) {
        result.error = ScriptDiagnostic{ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String("typed module id is empty", &core::defaultCpuMemoryResource())};
        return result;
    }

    ScriptDiagnostic error;
    auto syntax = compileToTypedAst(std::move(source), std::move(filename), &error, moduleRegistry);
    if (syntax == nullptr) {
        result.error = std::move(error);
        return result;
    }
    result.module.syntax = std::move(syntax);
    result.module.moduleId = std::move(moduleId);
    if (!CallableBindingCollector::collect(result.module, moduleRegistry, result.error))
        return result;
    return result;
}

ScriptFrontendResult ScriptFrontend::adoptAnalyzedSyntax(
    std::shared_ptr<Module> syntax, core::pmr::String moduleId, ModuleRegistry* moduleRegistry)
{
    ScriptFrontendResult result;
    if (syntax == nullptr || moduleId.empty()) {
        result.error = ScriptDiagnostic{ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String("analyzed syntax adoption requires module and id",
                &core::defaultCpuMemoryResource())};
        return result;
    }
    result.module.syntax = std::move(syntax);
    result.module.moduleId = std::move(moduleId);
    if (!CallableBindingCollector::collect(result.module, moduleRegistry, result.error))
        return result;
    return result;
}

TypedIrBuildResult ScriptLowerer::lower(const ScriptTypedModule& module)
{
    TypedIrBuildResult result;
    if (!module.valid()) {
        result.error = ScriptDiagnostic{ScriptDiagnosticCode::TypedIrInvalid,
            core::pmr::String("typed module is incomplete", &core::defaultCpuMemoryResource())};
        return result;
    }

    return DirectModuleLowerer(module).run();
}

} // namespace arp::scriptvm

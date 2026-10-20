#include "tools/asset_importer/script_runner.h"

#include "core/file_io.h"
#include "core/sha256.h"
#include "scriptvm/aot/aot_dynamic_package.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/compile/parser.h"
#include "scriptvm/compile/sema.h"
#include "scriptvm/interpreter/eval.h"
#include "scriptvm/interpreter/pipeline.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/semantic_runtime.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace arp::tools::asset_importer {

#if defined(ARP_SCRIPT_AOT_ONLY_TOOL)

extern "C" int arp_register_script_aot_package_v5(scriptvm::AotRegistrationHostV5* host);

namespace {

core::pmr::String aotDiagnostic(const scriptvm::VmError& error)
{
    core::pmr::String result(error.message, &core::defaultCpuMemoryResource());
    if (!error.traceback.empty()) {
        result += "\n";
        result += error.traceback;
    }
    return result;
}

class AotToolHostBindings final {
public:
    explicit AotToolHostBindings(scriptvm::CppPluginImporterDesc descriptor)
        : importer_(std::move(descriptor))
    {}

    static bool invoke(void* userData,
        const char*,
        const char* targetId,
        const scriptvm::Value* receiver,
        const scriptvm::Value* arguments,
        std::size_t argumentCount,
        scriptvm::Value* result,
        scriptvm::VmError* error)
    {
        if (userData == nullptr)
            return false;
        auto& bindings = *static_cast<AotToolHostBindings*>(userData);
        const bool succeeded =
            bindings.invoke(targetId, receiver, arguments, argumentCount, result, error);
        if (succeeded && result != nullptr)
            bindings.retainQuotaOwnedResult(*result);
        return succeeded;
    }

    static bool loadExport(void* userData,
        const char* moduleId,
        const char* exportName,
        scriptvm::Value* result,
        scriptvm::VmError* error)
    {
        return userData != nullptr
            && static_cast<AotToolHostBindings*>(userData)->loadExport(
                moduleId, exportName, result, error);
    }

    static bool describeValue(void* userData,
        const scriptvm::Value& value,
        scriptvm::ScriptValueMemoryDescriptor& descriptor) noexcept
    {
        return userData != nullptr
            && static_cast<AotToolHostBindings*>(userData)->describeValue(value, descriptor);
    }

private:
    static void setError(
        scriptvm::VmError* error, scriptvm::ScriptDiagnosticCode code, const char* message)
    {
        if (error == nullptr)
            return;
        error->code = code;
        error->message = message;
    }

    bool loadExport(const char* moduleId,
        const char* exportName,
        scriptvm::Value* result,
        scriptvm::VmError* error)
    {
        if (moduleId == nullptr || exportName == nullptr || result == nullptr) {
            setError(error,
                scriptvm::ScriptDiagnosticCode::AotArtifactInvalid,
                "AotOnly host module export identity is incomplete");
            return false;
        }
        const auto module =
            importer_.loadModule(core::pmr::String(moduleId, &core::defaultCpuMemoryResource()));
        if (module == nullptr) {
            setError(error,
                scriptvm::ScriptDiagnosticCode::AotModuleUnavailable,
                importer_.lastDiagnostics().empty() ? "AotOnly host module is unavailable"
                                                    : importer_.lastDiagnostics().c_str());
            return false;
        }
        const auto found =
            module->symbols.find(core::pmr::String(exportName, &core::defaultCpuMemoryResource()));
        if (found == module->symbols.end()) {
            setError(error,
                scriptvm::ScriptDiagnosticCode::AotMissingEntry,
                "AotOnly host module export is unavailable");
            return false;
        }
        if (std::find(retainedModules_.begin(), retainedModules_.end(), module)
            == retainedModules_.end()) {
            retainedModules_.push_back(module);
        }
        *result = found->second;
        return true;
    }

    bool describeValue(const scriptvm::Value& value,
        scriptvm::ScriptValueMemoryDescriptor& descriptor) const noexcept
    {
        const void* identity = value.storageIdentity();
        if (identity == nullptr)
            return false;
        const auto quotaOwned = quotaOwnedResults_.find(identity);
        if (quotaOwned != quotaOwnedResults_.end()
            && quotaOwned->second.storageIdentity() == identity) {
            descriptor.allocationIdentity = identity;
            descriptor.retainedBytes = 0u;
            descriptor.externallyLeased = true;
            return true;
        }
        if (value.is_native_reference()) {
            // Native references are engine-owned. The Value control block is
            // still a stable allocator identity, while the pointed-to native
            // object must not be charged to the script's retained graph.
            descriptor.allocationIdentity = identity;
            descriptor.retainedBytes = 0u;
            descriptor.externallyLeased = true;
            return true;
        }
        for (const auto& module : retainedModules_) {
            if (module == nullptr)
                continue;
            for (const auto& [name, symbol] : module->symbols) {
                (void)name;
                if (symbol.storageIdentity() != identity)
                    continue;
                // The outer Value allocation is charged by the accountant.
                // Its callable/reflection graph already belongs to this
                // explicitly retained ModuleExports owner, so the AOT copy
                // adds no separately retained allocation.
                descriptor.allocationIdentity = identity;
                descriptor.retainedBytes = 0u;
                descriptor.externallyLeased = true;
                return true;
            }
        }
        const auto charge = core::inspectCpuAllocationCharge(identity);
        if (value.class_() != nullptr && !value.is_native_reference() && charge.found
            && charge.quota != nullptr) {
            // Reflected by-value fields can be materialized by the shared
            // semantic runtime without crossing the bound-call callback. The
            // exact core allocation header retains its quota for the complete
            // Value lifetime, including allocator-aware constructor storage.
            descriptor.allocationIdentity = identity;
            descriptor.retainedBytes = 0u;
            descriptor.externallyLeased = true;
            return true;
        }
        return false;
    }

    void retainQuotaOwnedResult(const scriptvm::Value& value)
    {
        const void* identity = value.storageIdentity();
        const auto charge = core::inspectCpuAllocationCharge(identity);
        if (identity == nullptr || value.class_() == nullptr || value.is_native_reference()
            || !charge.found || charge.quota == nullptr) {
            return;
        }
        // Host calls execute inside the active DirectNative quota scope.
        // Retain the exact returned Value so its allocator identity cannot be
        // recycled while the descriptor is in use. Engine reflected value
        // fields are allocator-aware, so their PMR allocations carry the same
        // quota lease as the outer by-value result.
        quotaOwnedResults_.try_emplace(identity, value);
    }

    bool invoke(const char* targetId,
        const scriptvm::Value* receiver,
        const scriptvm::Value* arguments,
        std::size_t argumentCount,
        scriptvm::Value* result,
        scriptvm::VmError* error)
    {
        if (targetId == nullptr || result == nullptr
            || (argumentCount != 0u && arguments == nullptr)) {
            setError(error,
                scriptvm::ScriptDiagnosticCode::AotArtifactInvalid,
                "AotOnly host callable identity is incomplete");
            return false;
        }
        core::pmr::Vector<scriptvm::Value> args{&core::defaultCpuMemoryResource()};
        args.reserve(argumentCount);
        for (std::size_t index = 0; index < argumentCount; ++index)
            args.push_back(arguments[index]);
        try {
            if (receiver != nullptr) {
                const auto separator = std::string_view(targetId).rfind('.');
                if (separator == std::string_view::npos) {
                    setError(error,
                        scriptvm::ScriptDiagnosticCode::AotArtifactInvalid,
                        "AotOnly reflection target has no member name");
                    return false;
                }
                const auto memberName = std::string_view(targetId).substr(separator + 1u);
                auto loaded = scriptvm::ScriptSemanticRuntime::loadField(
                    *receiver, core::pmr::String(memberName, &core::defaultCpuMemoryResource()));
                if (!loaded.ok()) {
                    setError(error, loaded.diagnostic.code, loaded.diagnostic.message.c_str());
                    return false;
                }
                if (loaded.value.is<scriptvm::ReflectedMethod>()) {
                    const auto& method = loaded.value.cast<scriptvm::ReflectedMethod&>();
                    *result = method.function.call_v(method.receiver, args);
                    return true;
                }
                if (loaded.value.is<arp::reflection::function>()) {
                    auto& function = loaded.value.cast<arp::reflection::function&>();
                    *result = function.call_v(*receiver, args);
                    return true;
                }
                setError(error,
                    scriptvm::ScriptDiagnosticCode::HostError,
                    "AotOnly reflection target is not callable");
                return false;
            }
            const auto separator = std::string_view(targetId).find('.');
            if (separator == std::string_view::npos) {
                setError(error,
                    scriptvm::ScriptDiagnosticCode::AotArtifactInvalid,
                    "AotOnly host target has no module identity");
                return false;
            }
            scriptvm::Value callable;
            const auto moduleName = std::string_view(targetId).substr(0u, separator);
            const auto exportName = std::string_view(targetId).substr(separator + 1u);
            if (!loadExport(
                    core::pmr::String(moduleName, &core::defaultCpuMemoryResource()).c_str(),
                    core::pmr::String(exportName, &core::defaultCpuMemoryResource()).c_str(),
                    &callable,
                    error)) {
                return false;
            }
            if (!callable.is<scriptvm::HostCallable>()) {
                if (callable.is<arp::reflection::function>()) {
                    auto& function = callable.cast<arp::reflection::function&>();
                    if (function.is_static()) {
                        *result = function.call_v(scriptvm::Value::undefined(), args);
                        return true;
                    }
                    if (args.empty()) {
                        setError(error,
                            scriptvm::ScriptDiagnosticCode::WrongArgCount,
                            "AotOnly reflected callable expects an instance");
                        return false;
                    }
                    const auto reflectedReceiver = args.front();
                    args.erase(args.begin());
                    *result = function.call_v(reflectedReceiver, args);
                    return true;
                }
                core::pmr::String diagnostic(
                    "AotOnly host export is not callable: ", &core::defaultCpuMemoryResource());
                diagnostic += targetId;
                setError(error, scriptvm::ScriptDiagnosticCode::HostError, diagnostic.c_str());
                return false;
            }
            const auto& host = callable.cast<scriptvm::HostCallable&>();
            if (!host.contract.acceptsArity(argumentCount)) {
                setError(error,
                    scriptvm::ScriptDiagnosticCode::WrongArgCount,
                    "AotOnly host callable arity mismatch");
                return false;
            }
            *result = host.fn(args);
            return true;
        } catch (const std::bad_alloc&) {
            throw;
        } catch (const std::exception& exception) {
            setError(error, scriptvm::ScriptDiagnosticCode::HostError, exception.what());
            return false;
        } catch (...) {
            setError(
                error, scriptvm::ScriptDiagnosticCode::HostError, "AotOnly host callable threw");
            return false;
        }
    }

    scriptvm::CppPluginImporter importer_;
    core::pmr::Vector<std::shared_ptr<scriptvm::ModuleExports>> retainedModules_{
        &core::defaultCpuMemoryResource()};
    core::pmr::UnorderedMap<const void*, scriptvm::Value> quotaOwnedResults_{
        &core::defaultCpuMemoryResource()};
};

} // namespace

ScriptToolRunner::ScriptToolRunner(std::filesystem::path projectRoot,
    std::filesystem::path pluginRoot,
    std::filesystem::path cacheRoot,
    std::filesystem::path hostLibrary,
    ScriptAotToolchain aotToolchain)
    : projectRoot_(std::move(projectRoot)), pluginRoot_(std::move(pluginRoot)),
      cacheRoot_(std::move(cacheRoot)), hostLibrary_(std::move(hostLibrary)),
      aotToolchain_(std::move(aotToolchain))
{
#if defined(ARP_SCRIPT_AOTC_PATH)
    if (aotToolchain_.aotCompiler.empty())
        aotToolchain_.aotCompiler = ARP_SCRIPT_AOTC_PATH;
#endif
#if defined(ARP_SCRIPT_AOT_CXX_PATH)
    if (aotToolchain_.cxxCompiler.empty())
        aotToolchain_.cxxCompiler = ARP_SCRIPT_AOT_CXX_PATH;
#endif
#if defined(ARP_SCRIPT_AOT_NM_PATH)
    if (aotToolchain_.symbolInspector.empty())
        aotToolchain_.symbolInspector = ARP_SCRIPT_AOT_NM_PATH;
#endif
#if defined(ARP_SCRIPT_AOT_OBJDUMP_PATH)
    if (aotToolchain_.objectInspector.empty())
        aotToolchain_.objectInspector = ARP_SCRIPT_AOT_OBJDUMP_PATH;
#endif
}

ScriptRunResult ScriptToolRunner::runPackageFunction(const std::filesystem::path& packageManifest,
    const char* moduleId,
    const char* functionName,
    const core::pmr::Vector<scriptvm::Value>& args)
{
    ScriptRunResult result;
    if (moduleId == nullptr || moduleId[0] == '\0' || functionName == nullptr
        || functionName[0] == '\0') {
        result.diagnostics = "AotOnly package module/function identity is incomplete";
        return result;
    }
    scriptvm::WindowsDynamicScriptAotPackageDesc descriptor;
    descriptor.manifest = packageManifest;
    descriptor.cacheRoot = cacheRoot_ / "script-aot-v5";
    descriptor.aotCompiler = aotToolchain_.aotCompiler;
    descriptor.cxxCompiler = aotToolchain_.cxxCompiler;
    descriptor.symbolInspector = aotToolchain_.symbolInspector;
    descriptor.objectInspector = aotToolchain_.objectInspector;
    descriptor.includeRoot = projectRoot_ / "src";
    descriptor.hostLibrary = hostLibrary_;

    scriptvm::ScriptAotPackageRegistry registry;
    const auto loaded = scriptvm::WindowsDynamicScriptAotPackageLoader().load(descriptor, registry);
    if (!loaded.status.ok()) {
        result.diagnostics = loaded.status.diagnostic().message;
        return result;
    }
    scriptvm::ScriptAotModuleInstanceV5 instance;
    const auto created = scriptvm::createAotOnlyModuleInstanceV5(
        registry, loaded.packageIdentity.c_str(), moduleId, instance);
    if (!created.ok()) {
        result.diagnostics = created.diagnostic().message;
        return result;
    }
    scriptvm::CppPluginImporterDesc pluginDesc;
    pluginDesc.sourceRoot =
        std::filesystem::exists(pluginRoot_) ? pluginRoot_ : projectRoot_ / "modules";
    pluginDesc.cacheRoot = cacheRoot_;
    pluginDesc.includeRoot = projectRoot_ / "src";
    pluginDesc.additionalIncludeRoots.push_back(projectRoot_ / "3rd" / "fkYAML" / "include");
    pluginDesc.hostLibrary = hostLibrary_;
    // AOT-only execution must use the immutable toolchain selected by the
    // build, rather than relying on the process PATH to resolve `g++`.
    pluginDesc.compilerCommand = core::pmr::String(
        aotToolchain_.cxxCompiler.generic_string(), &core::defaultCpuMemoryResource());
    pluginDesc.moduleDirectoryLayout = true;
    AotToolHostBindings bindings(std::move(pluginDesc));
    if (!scriptvm::configureAotOnlyBoundCallV5(instance, &AotToolHostBindings::invoke, &bindings)
            .ok()
        || !scriptvm::configureAotOnlyModuleExportV5(
            instance, &AotToolHostBindings::loadExport, &bindings)
            .ok()) {
        result.diagnostics = "failed to configure dynamic AotOnly host contracts";
        return result;
    }
    scriptvm::DirectNativeDispatcherV5 dispatcher(std::move(instance));
    dispatcher.setValueMemoryDescriptor(&AotToolHostBindings::describeValue, &bindings);
    core::pmr::String functionId("arp-fn:", &core::defaultCpuMemoryResource());
    functionId += moduleId;
    functionId += "/";
    functionId += functionName;
    scriptvm::VmError error;
    result.value = dispatcher.invoke(functionId.c_str(), args.data(), args.size(), &error);
    if (error.code != scriptvm::ScriptDiagnosticCode::None) {
        result.diagnostics = aotDiagnostic(error);
        return result;
    }
    result.success = true;
    return result;
}

ScriptRunResult ScriptToolRunner::runFile(
    const std::filesystem::path& scriptPath, const core::pmr::Vector<scriptvm::Value>& args)
{
    return runFileFunction(scriptPath, "main", args);
}

ScriptRunResult ScriptToolRunner::runFileFunction(const std::filesystem::path& scriptPath,
    const char* functionName,
    const core::pmr::Vector<scriptvm::Value>& args)
{
    ScriptRunResult result;
    const auto filename = scriptPath.filename().generic_string();
    if (filename != "unity_import.arp" && filename != "script_unity_import.arp") {
        result.diagnostics = "AotOnly tool script is not declared by the unity-import package";
        return result;
    }
    scriptvm::ScriptAotPackageRegistry registry;
    auto host = registry.registrationHost();
    if (arp_register_script_aot_package_v5(&host) != 1) {
        result.diagnostics = "failed to register the static unity-import AOT v5 package";
        return result;
    }
    // The generated package is the sole registry entry. Its identity is
    // obtained through registration capture rather than inferred from source.
    const scriptvm::AotPackageDescriptorV5* registered = nullptr;
    scriptvm::AotRegistrationHostV5 captureHost{scriptvm::kAotContractVersion,
        [](const scriptvm::AotPackageDescriptorV5* value, void* userData) {
            *static_cast<const scriptvm::AotPackageDescriptorV5**>(userData) = value;
            return value != nullptr;
        },
        &registered};
    if (arp_register_script_aot_package_v5(&captureHost) != 1 || registered == nullptr) {
        result.diagnostics = "failed to resolve the static unity-import package identity";
        return result;
    }
    scriptvm::ScriptAotModuleInstanceV5 instance;
    const auto created = scriptvm::createAotOnlyModuleInstanceV5(
        registry, registered->packageIdentity, "unity_import", instance);
    if (!created.ok()) {
        result.diagnostics = created.diagnostic().message;
        return result;
    }
    scriptvm::CppPluginImporterDesc pluginDesc;
    pluginDesc.sourceRoot =
        std::filesystem::exists(pluginRoot_) ? pluginRoot_ : projectRoot_ / "modules";
    pluginDesc.cacheRoot = cacheRoot_;
    pluginDesc.includeRoot = projectRoot_ / "src";
    pluginDesc.additionalIncludeRoots.push_back(projectRoot_ / "3rd" / "fkYAML" / "include");
    pluginDesc.hostLibrary = hostLibrary_;
    // AOT-only execution must use the immutable toolchain selected by the
    // build, rather than relying on the process PATH to resolve `g++`.
    pluginDesc.compilerCommand = core::pmr::String(
        aotToolchain_.cxxCompiler.generic_string(), &core::defaultCpuMemoryResource());
    pluginDesc.moduleDirectoryLayout = true;
    AotToolHostBindings bindings(std::move(pluginDesc));
    if (!scriptvm::configureAotOnlyBoundCallV5(instance, &AotToolHostBindings::invoke, &bindings)
            .ok()
        || !scriptvm::configureAotOnlyModuleExportV5(
            instance, &AotToolHostBindings::loadExport, &bindings)
            .ok()) {
        result.diagnostics = "failed to configure AotOnly host contracts";
        return result;
    }
    scriptvm::DirectNativeDispatcherV5 dispatcher(std::move(instance));
    dispatcher.setValueMemoryDescriptor(&AotToolHostBindings::describeValue, &bindings);
    core::pmr::String functionId("arp-fn:unity_import/", &core::defaultCpuMemoryResource());
    functionId += functionName;
    scriptvm::VmError error;
    result.value = dispatcher.invoke(functionId.c_str(), args.data(), args.size(), &error);
    if (error.code != scriptvm::ScriptDiagnosticCode::None) {
        result.diagnostics = aotDiagnostic(error);
        return result;
    }
    result.success = true;
    return result;
}

#else

namespace {

struct SourceModuleLifetime final {
    scriptvm::Program program;
};

core::pmr::String diagnosticText(const scriptvm::ScriptDiagnostic& diagnostic)
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    if (!diagnostic.file.empty()) {
        result += diagnostic.file;
        result += ":";
    }
    if (diagnostic.line > 0) {
        result += std::to_string(diagnostic.line);
        result += ":";
        result += std::to_string(diagnostic.column);
        result += " ";
    }
    result += diagnostic.message;
    return result;
}

core::pmr::String sha256Contract(const core::pmr::String& text)
{
    const auto digest = core::Sha256::digest(core::StringView(text.data(), text.size()));
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(71u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

bool buildModuleInputHashes(const std::shared_ptr<scriptvm::Module>& module,
    scriptvm::ModuleRegistry& registry,
    const core::pmr::String& sourceDependencyContract,
    core::pmr::String& dependencyGraphHash,
    core::pmr::String& reflectionRegistryHash,
    core::pmr::String& diagnostics)
{
    core::pmr::Vector<core::pmr::String> moduleNames{&core::defaultCpuMemoryResource()};
    for (const auto& declaration : module->decls) {
        if (declaration->kind != scriptvm::StmtKind::ImportDecl
            || declaration->importModule.empty())
            continue;
        if (std::find(moduleNames.begin(), moduleNames.end(), declaration->importModule)
            == moduleNames.end())
            moduleNames.push_back(declaration->importModule);
    }
    std::sort(moduleNames.begin(), moduleNames.end());

    core::pmr::String dependencyContract(
        "scriptvm-dependency-graph-v1\n", &core::defaultCpuMemoryResource());
    dependencyContract += sourceDependencyContract;
    core::pmr::String reflectionContract(
        "scriptvm-reflection-registry-v1\n", &core::defaultCpuMemoryResource());
    for (const auto& moduleName : moduleNames) {
        const auto exports = registry.loadModule(moduleName);
        if (exports == nullptr || exports->moduleVersion == 0u
            || exports->exportContractHash.empty() || exports->reflectionRegistryHash.empty()) {
            diagnostics = "imported module has incomplete identity contract: ";
            diagnostics += moduleName;
            return false;
        }
        dependencyContract += moduleName;
        dependencyContract += "\nversion=";
        dependencyContract += std::to_string(exports->moduleVersion);
        dependencyContract += "\nexports=";
        dependencyContract += exports->exportContractHash;
        dependencyContract += "\n";
        reflectionContract += moduleName;
        reflectionContract += "=";
        reflectionContract += exports->reflectionRegistryHash;
        reflectionContract += "\n";
    }
    dependencyGraphHash = sha256Contract(dependencyContract);
    reflectionRegistryHash = sha256Contract(reflectionContract);
    return true;
}

core::pmr::String diagnosticText(const scriptvm::VmError& error)
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    result += error.message;
    if (!error.traceback.empty()) {
        if (!result.empty()) {
            result += "\n";
        }
        result += error.traceback;
    }
    return result;
}

bool constantValue(const scriptvm::StmtPtr& declaration, scriptvm::Value& value)
{
    if (!declaration || declaration->kind != scriptvm::StmtKind::VarDecl || !declaration->isConst
        || !declaration->expr) {
        return false;
    }
    switch (declaration->expr->kind) {
    case scriptvm::ExprKind::IntLit:
        value = scriptvm::makeInt(static_cast<int>(declaration->expr->intVal));
        return true;
    case scriptvm::ExprKind::Int64Lit:
        value = scriptvm::makeInt64(declaration->expr->intVal);
        return true;
    case scriptvm::ExprKind::FloatLit:
        value = scriptvm::makeFloat(declaration->expr->floatVal);
        return true;
    case scriptvm::ExprKind::StringLit:
        value = scriptvm::Value{declaration->expr->strVal};
        return true;
    case scriptvm::ExprKind::BoolLit:
        value = scriptvm::Value{declaration->expr->boolVal};
        return true;
    default:
        return false;
    }
}

bool hasClassDeclaration(const std::shared_ptr<scriptvm::Module>& module)
{
    for (const auto& declaration : module->decls) {
        if (declaration->kind == scriptvm::StmtKind::ClassDecl)
            return true;
    }
    return false;
}

bool containsModule(
    const core::pmr::Vector<core::pmr::String>& modules, const core::pmr::String& name)
{
    for (const auto& module : modules) {
        if (module == name)
            return true;
    }
    return false;
}

bool sameTopLevelDeclaration(const scriptvm::StmtPtr& left, const scriptvm::StmtPtr& right)
{
    if (left->kind != right->kind)
        return false;
    switch (left->kind) {
    case scriptvm::StmtKind::ImportDecl:
        if (left->importModule != right->importModule
            || left->importIsModuleImport != right->importIsModuleImport
            || left->importAlias != right->importAlias
            || left->importNames.size() != right->importNames.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left->importNames.size(); ++index) {
            if (left->importNames[index].name != right->importNames[index].name
                || left->importNames[index].alias != right->importNames[index].alias) {
                return false;
            }
        }
        return true;
    case scriptvm::StmtKind::ClassDecl:
        return left->className == right->className;
    case scriptvm::StmtKind::FuncDecl:
        return left->funcName == right->funcName;
    case scriptvm::StmtKind::VarDecl:
        return left->varName == right->varName;
    default:
        return false;
    }
}

void appendUniqueDeclaration(
    core::pmr::Vector<scriptvm::StmtPtr>& declarations, const scriptvm::StmtPtr& declaration)
{
    for (const auto& existing : declarations) {
        if (sameTopLevelDeclaration(existing, declaration))
            return;
    }
    declarations.push_back(declaration);
}

bool mergeSourceClassDependencies(const std::filesystem::path& sourceRoot,
    const std::shared_ptr<scriptvm::Module>& module,
    core::pmr::Vector<core::pmr::String>& resolving,
    core::pmr::String& dependencyContract,
    core::pmr::String& diagnostics)
{
    core::pmr::Vector<scriptvm::StmtPtr> merged{&core::defaultCpuMemoryResource()};
    for (const auto& declaration : module->decls) {
        if (declaration->kind != scriptvm::StmtKind::ImportDecl
            || declaration->importModule.empty()) {
            appendUniqueDeclaration(merged, declaration);
            continue;
        }

        const auto dependencyPath = sourceRoot
            / (std::string(declaration->importModule.data(), declaration->importModule.size())
                + ".arp");
        if (!std::filesystem::exists(dependencyPath)) {
            appendUniqueDeclaration(merged, declaration);
            continue;
        }
        if (containsModule(resolving, declaration->importModule)) {
            diagnostics = "circular source module import: ";
            diagnostics += declaration->importModule;
            return false;
        }

        const auto source = core::readTextFile(dependencyPath);
        if (!source.success) {
            diagnostics = source.diagnostics;
            return false;
        }
        dependencyContract += declaration->importModule;
        dependencyContract += "=";
        dependencyContract += sha256Contract(source.text);
        dependencyContract += "\n";
        auto parsed =
            scriptvm::parseSource(source.text, core::toPmrString(dependencyPath.generic_string()));
        if (!parsed.ok() || !parsed.module) {
            diagnostics = parsed.ok()
                ? core::pmr::String("source dependency parser returned no module",
                      &core::defaultCpuMemoryResource())
                : diagnosticText(parsed.error);
            return false;
        }

        resolving.push_back(declaration->importModule);
        if (!mergeSourceClassDependencies(
                sourceRoot, parsed.module, resolving, dependencyContract, diagnostics)) {
            resolving.pop_back();
            return false;
        }
        resolving.pop_back();

        if (!hasClassDeclaration(parsed.module)) {
            appendUniqueDeclaration(merged, declaration);
            continue;
        }

        // Source modules containing classes are bundled into the importing
        // program so script class identity, inheritance, and method indices
        // remain owned by one Program. Their declarations are deduplicated
        // because host imports can already exist in the importing module.
        for (const auto& dependencyDeclaration : parsed.module->decls) {
            appendUniqueDeclaration(merged, dependencyDeclaration);
        }
    }
    module->decls = std::move(merged);
    return true;
}

} // namespace

ScriptToolRunner::ScriptToolRunner(std::filesystem::path projectRoot,
    std::filesystem::path pluginRoot,
    std::filesystem::path cacheRoot,
    std::filesystem::path hostLibrary,
    ScriptAotToolchain aotToolchain)
    : projectRoot_(std::move(projectRoot)), pluginRoot_(std::move(pluginRoot)),
      cacheRoot_(std::move(cacheRoot)), hostLibrary_(std::move(hostLibrary)),
      aotToolchain_(std::move(aotToolchain))
{
    scriptvm::CppPluginImporterDesc pluginDesc;
    pluginDesc.sourceRoot =
        std::filesystem::exists(pluginRoot_) ? pluginRoot_ : projectRoot_ / "modules";
    pluginDesc.cacheRoot = cacheRoot_;
    pluginDesc.includeRoot = projectRoot_ / "src";
    pluginDesc.additionalIncludeRoots.push_back(projectRoot_ / "3rd" / "fkYAML" / "include");
    pluginDesc.hostLibrary = hostLibrary_;
#if defined(ARP_SCRIPT_CPP_COMPILER_PATH)
    pluginDesc.compilerCommand =
        core::pmr::String(ARP_SCRIPT_CPP_COMPILER_PATH, &core::defaultCpuMemoryResource());
#endif
    pluginDesc.moduleDirectoryLayout = true;
    pluginImporter_ = core::makeUnique<scriptvm::CppPluginImporter>(std::move(pluginDesc));
}

ScriptRunResult ScriptToolRunner::runFile(
    const std::filesystem::path& scriptPath, const core::pmr::Vector<scriptvm::Value>& args)
{
    return runFileFunction(scriptPath, "main", args);
}

ScriptRunResult ScriptToolRunner::runFileFunction(const std::filesystem::path& scriptPath,
    const char* functionName,
    const core::pmr::Vector<scriptvm::Value>& args)
{
    ScriptRunResult result;
    const auto source = core::readTextFile(scriptPath);
    if (!source.success) {
        result.diagnostics = source.diagnostics;
        return result;
    }

    auto& pluginImporter = *pluginImporter_;
    scriptvm::ModuleRegistry modules;
    modules.setCppPluginImporter(&pluginImporter);
    const auto sourceModuleRoot = projectRoot_ / "tools" / "unity_import";
    core::pmr::String sourceModuleDiagnostics{&core::defaultCpuMemoryResource()};
    modules.setSourceResolver([sourceModuleRoot,
                                  &modules,
                                  &sourceModuleDiagnostics,
                                  &pluginImporter](const core::pmr::String& moduleName)
                                  -> std::shared_ptr<scriptvm::ModuleExports> {
        sourceModuleDiagnostics.clear();
        const auto fail = [&](const char* message) -> std::shared_ptr<scriptvm::ModuleExports> {
            sourceModuleDiagnostics = message;
            return nullptr;
        };
        if (moduleName.empty()) {
            return fail("source module name is empty");
        }
        for (const char ch : moduleName) {
            if (!(ch == '_' || ch == '-' || ch == '.' || (ch >= 'a' && ch <= 'z')
                    || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
                return fail("source module name contains an invalid character");
            }
        }

        const auto path =
            sourceModuleRoot / (std::string(moduleName.data(), moduleName.size()) + ".arp");
        if (!std::filesystem::exists(path)) {
            return nullptr;
        }
        const auto source = core::readTextFile(path);
        if (!source.success) {
            sourceModuleDiagnostics = source.diagnostics;
            return nullptr;
        }
        auto parsed = scriptvm::parseSource(source.text, core::toPmrString(path.generic_string()));
        if (!parsed.ok() || !parsed.module) {
            sourceModuleDiagnostics = parsed.ok()
                ? core::pmr::String(
                      "source module parser returned no module", &core::defaultCpuMemoryResource())
                : diagnosticText(parsed.error);
            return nullptr;
        }
        core::pmr::Vector<core::pmr::String> resolving{&core::defaultCpuMemoryResource()};
        core::pmr::String sourceDependencyContract{&core::defaultCpuMemoryResource()};
        resolving.push_back(moduleName);
        if (!mergeSourceClassDependencies(sourceModuleRoot,
                parsed.module,
                resolving,
                sourceDependencyContract,
                sourceModuleDiagnostics)) {
            return nullptr;
        }
        const auto analyzed = scriptvm::analyze(parsed.module, &modules);
        if (!analyzed.ok()) {
            sourceModuleDiagnostics = diagnosticText(analyzed.error);
            if (!modules.lastDiagnostics().empty()) {
                sourceModuleDiagnostics += "\nmodule loader: ";
                sourceModuleDiagnostics += modules.lastDiagnostics();
            }
            if (!pluginImporter.lastDiagnostics().empty()) {
                sourceModuleDiagnostics += "\nC++ plugin: ";
                sourceModuleDiagnostics += pluginImporter.lastDiagnostics();
            }
            return nullptr;
        }
        core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
        core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
        if (!buildModuleInputHashes(parsed.module,
                modules,
                sourceDependencyContract,
                dependencyGraphHash,
                reflectionRegistryHash,
                sourceModuleDiagnostics)) {
            return nullptr;
        }
        auto frontend =
            scriptvm::ScriptFrontend::adoptAnalyzedSyntax(parsed.module, moduleName, &modules);
        if (!frontend.ok()) {
            sourceModuleDiagnostics = diagnosticText(frontend.error);
            return nullptr;
        }
        scriptvm::ScriptInterpreterPreparedBuildRequest buildRequest;
        buildRequest.source = source.text;
        buildRequest.module = std::move(frontend.module);
        buildRequest.moduleVersion = 1u;
        buildRequest.dependencyGraphHash = dependencyGraphHash;
        buildRequest.reflectionRegistryHash = reflectionRegistryHash;
        const auto compiled = scriptvm::ScriptInterpreterPipeline::buildPrepared(buildRequest);
        if (!compiled.ok()) {
            sourceModuleDiagnostics = diagnosticText(compiled.error);
            return nullptr;
        }

        auto lifetime = core::makeShared<SourceModuleLifetime>();
        lifetime->program = compiled.program;
        auto exports = core::makeShared<scriptvm::ModuleExports>();
        exports->moduleVersion = 1u;
        exports->exportContractHash = compiled.typedIr.fingerprint;
        exports->reflectionRegistryHash = reflectionRegistryHash;
        for (const auto& declaration : parsed.module->decls) {
            scriptvm::Value value;
            if (constantValue(declaration, value)) {
                exports->symbols[declaration->varName] = std::move(value);
            }
        }
        for (const auto& declaration : parsed.module->decls) {
            if (declaration->kind != scriptvm::StmtKind::FuncDecl) {
                continue;
            }
            scriptvm::ScriptFunctionContract contract;
            contract.isVariadic = declaration->variadicParam;
            const std::size_t fixedCount = declaration->variadicParam
                ? static_cast<std::size_t>(std::max(0, declaration->variadicParamIndex))
                : declaration->paramTypes.size();
            for (std::size_t index = 0;
                index < fixedCount && index < declaration->paramTypes.size();
                ++index) {
                contract.params.push_back(declaration->paramTypes[index]);
            }
            if (declaration->variadicParam && declaration->variadicParamIndex >= 0
                && static_cast<std::size_t>(declaration->variadicParamIndex)
                    < declaration->paramTypes.size()) {
                contract.params.push_back(declaration
                        ->paramTypes[static_cast<std::size_t>(declaration->variadicParamIndex)]);
            }
            contract.result = declaration->retType;
            exports->scriptFunctionContracts.emplace(declaration->funcName, std::move(contract));
        }
        for (const auto& [name, index] : lifetime->program.funcIndex) {
            if (name == "__module_init__" || name == "__entry__") {
                continue;
            }
            const auto capturedIndex = index;
            scriptvm::ModuleRegistry* registry = &modules;
            scriptvm::HostFn function =
                [lifetime, capturedIndex, registry](
                    const core::pmr::Vector<scriptvm::Value>& args) -> scriptvm::Value {
                const auto evaluated =
                    scriptvm::runFunction(lifetime->program, capturedIndex, args, registry);
                if (!evaluated.ok()) {
                    const auto diagnostic = diagnosticText(evaluated.error);
                    throw scriptvm::ScriptHostException(
                        diagnostic.empty() ? "source module function failed" : diagnostic.c_str());
                }
                return evaluated.value;
            };
            exports->symbols[name] = scriptvm::Value{scriptvm::HostCallable(std::move(function))};
        }
        exports->nativeLifetime = lifetime;
        return exports;
    });

    auto parsed =
        scriptvm::parseSource(source.text, core::toPmrString(scriptPath.generic_string()));
    if (!parsed.ok()) {
        result.diagnostics = diagnosticText(parsed.error);
        return result;
    }
    // The entry script keeps source-module imports as module bindings. Class
    // dependencies are expanded only while compiling the imported source
    // module itself, where inheritance needs one shared Program.
    const auto analyzed = scriptvm::analyze(parsed.module, &modules);
    if (!analyzed.ok()) {
        result.diagnostics = diagnosticText(analyzed.error);
        if (!sourceModuleDiagnostics.empty()) {
            result.diagnostics += "\nsource module: ";
            result.diagnostics += sourceModuleDiagnostics;
        }
        if (!modules.lastDiagnostics().empty()) {
            result.diagnostics += "\nmodule loader: ";
            result.diagnostics += modules.lastDiagnostics();
        }
        return result;
    }
    core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceDependencyContract{&core::defaultCpuMemoryResource()};
    if (!buildModuleInputHashes(parsed.module,
            modules,
            sourceDependencyContract,
            dependencyGraphHash,
            reflectionRegistryHash,
            result.diagnostics)) {
        return result;
    }
    auto frontend = scriptvm::ScriptFrontend::adoptAnalyzedSyntax(
        parsed.module, core::toPmrString(scriptPath.generic_string()), &modules);
    if (!frontend.ok()) {
        result.diagnostics = diagnosticText(frontend.error);
        return result;
    }
    scriptvm::ScriptInterpreterPreparedBuildRequest buildRequest;
    buildRequest.source = source.text;
    buildRequest.module = std::move(frontend.module);
    buildRequest.moduleVersion = 1u;
    buildRequest.dependencyGraphHash = dependencyGraphHash;
    buildRequest.reflectionRegistryHash = reflectionRegistryHash;
    const auto compiled = scriptvm::ScriptInterpreterPipeline::buildPrepared(buildRequest);
    if (!compiled.ok()) {
        result.diagnostics = diagnosticText(compiled.error);
        return result;
    }

    scriptvm::EvalResult evaluated;
    try {
        evaluated = scriptvm::runFunctionByName(compiled.program,
            core::pmr::String(functionName, &core::defaultCpuMemoryResource()),
            args,
            &modules);
    } catch (const std::exception& error) {
        result.diagnostics = error.what();
        return result;
    }
    if (!evaluated.ok()) {
        result.diagnostics = diagnosticText(evaluated.error);
        if (!sourceModuleDiagnostics.empty()) {
            result.diagnostics += "\nsource module: ";
            result.diagnostics += sourceModuleDiagnostics;
        }
        if (evaluated.error.code == scriptvm::ScriptDiagnosticCode::HostError
            && !pluginImporter.lastDiagnostics().empty()) {
            if (!result.diagnostics.empty()) {
                result.diagnostics += "\n";
            }
            result.diagnostics += pluginImporter.lastDiagnostics();
        }
        return result;
    }
    result.success = true;
    result.value = evaluated.value;
    return result;
}

ScriptRunResult ScriptToolRunner::runPackageFunction(const std::filesystem::path&,
    const char*,
    const char*,
    const core::pmr::Vector<scriptvm::Value>&)
{
    ScriptRunResult result;
    result.diagnostics = "dynamic AOT packages require an AotOnly Windows tool build";
    return result;
}

#endif

} // namespace arp::tools::asset_importer

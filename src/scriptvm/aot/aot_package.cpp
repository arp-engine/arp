#include "scriptvm/aot/aot_package.h"

#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/text/number_format.h"
#include "scriptvm/compile/frontend.h"
#include "scriptvm/compile/parser.h"
#include "scriptvm/compile/sema.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fkYAML/node.hpp>
#include <fstream>
#include <limits>
#include <system_error>

namespace arp::scriptvm {
namespace {

using YamlNode = fkyaml::node;
using String = core::pmr::String;

const YamlNode* field(const YamlNode& mapping, const char* name)
{
    if (!mapping.is_mapping())
        return nullptr;
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(name));
    return found == values.end() ? nullptr : &found->second;
}

String stringValue(const YamlNode& node)
{
    const auto& value = node.get_value_ref<const YamlNode::string_type&>();
    return String(value.data(), value.size(), &core::defaultCpuMemoryResource());
}

bool unsignedValue(const YamlNode* node, std::uint64_t& output)
{
    if (node == nullptr || !node->is_integer())
        return false;
    const auto value = node->get_value<YamlNode::integer_type>();
    if (value <= 0)
        return false;
    output = static_cast<std::uint64_t>(value);
    return true;
}

bool stringSequence(const YamlNode* node, core::pmr::Vector<String>& output, bool required)
{
    if (node == nullptr)
        return !required;
    if (!node->is_sequence())
        return false;
    for (const auto& value : node->get_value_ref<const YamlNode::sequence_type&>()) {
        if (!value.is_string())
            return false;
        output.push_back(stringValue(value));
    }
    return !required || !output.empty();
}

ScriptDiagnostic error(const char* message, const std::filesystem::path& path = {})
{
    ScriptDiagnostic result{ScriptDiagnosticCode::AotArtifactInvalid,
        String(message, &core::defaultCpuMemoryResource())};
    if (!path.empty()) {
        const auto text = path.generic_string();
        result.file.assign(text.data(), text.size());
    }
    return result;
}

String sha256(const String& value)
{
    static constexpr char digits[] = "0123456789abcdef";
    const auto digest = core::Sha256::digest(core::StringView(value.data(), value.size()));
    String result("sha256:", &core::defaultCpuMemoryResource());
    for (const auto byte : digest) {
        result += digits[(byte >> 4u) & 0xfu];
        result += digits[byte & 0xfu];
    }
    return result;
}

enum class ContractHashKind {
    DependencyGraph,
    ReflectionRegistry,
    HostContract,
};

String contractHash(const ScriptAotPackageDefinition& package, ContractHashKind kind)
{
    String canonical("arp.script.aot-contract/v1\n", &core::defaultCpuMemoryResource());
    if (kind == ContractHashKind::DependencyGraph) {
        canonical += "dependency-graph\n";
        for (const auto& module : package.modules) {
            canonical += module.moduleId;
            canonical += "\nversion:";
            core::appendUnsigned(canonical, module.moduleVersion);
            canonical += "\n";
            for (const auto& dependency : module.dependencies) {
                canonical += "dependency:";
                canonical += dependency;
                canonical += "\n";
            }
            for (const auto& dependency : module.pluginDependencies) {
                canonical += "plugin:";
                canonical += dependency.moduleId;
                canonical += "\nversion:";
                core::appendUnsigned(canonical, dependency.moduleVersion);
                canonical += "\nexport:";
                canonical += dependency.exportContractHash;
                canonical += "\nreflection:";
                canonical += dependency.reflectionRegistryHash;
                canonical += "\n";
                for (const auto& service : dependency.services) {
                    canonical += "service:";
                    canonical += service.serviceId;
                    canonical += "@";
                    core::appendUnsigned(canonical, service.version);
                    canonical += "\n";
                }
            }
        }
    } else {
        canonical += kind == ContractHashKind::ReflectionRegistry ? "reflection-registry\n"
                                                                  : "host-contract\n";
        const auto expectedOrigin = kind == ContractHashKind::ReflectionRegistry
            ? TypedIrCallableOrigin::ReflectionMember
            : TypedIrCallableOrigin::HostCapability;
        for (const auto& callable : package.callableContracts) {
            if (callable.origin != expectedOrigin)
                continue;
            canonical += "binding:";
            canonical += callable.bindingId;
            canonical += "\ntarget:";
            canonical += callable.targetId;
            canonical += "\nsignature:";
            canonical += callable.signatureHash;
            canonical += "\norigin:";
            core::appendUnsigned(canonical, static_cast<std::uint32_t>(callable.origin));
            canonical += "\narity:";
            core::appendUnsigned(canonical, callable.fixedArity);
            canonical += "\nmay-suspend:";
            canonical += callable.maySuspend ? "1" : "0";
            canonical += "\nlifetime:";
            core::appendUnsigned(canonical, callable.lifetimeVersion);
            canonical += "\n";
        }
    }
    return sha256(canonical);
}

ScriptResult contractHashMismatch(const char* name, const String& expected, const String& actual)
{
    String message("AOT ", &core::defaultCpuMemoryResource());
    message += name;
    message += " does not match manifest contents; expected ";
    message += expected;
    message += ", got ";
    message += actual;
    return ScriptResult::failure(ScriptDiagnosticCode::AotArtifactInvalid, std::move(message));
}

String pathText(const std::filesystem::path& path)
{
    const auto value = path.generic_string();
    return String(value.data(), value.size(), &core::defaultCpuMemoryResource());
}

bool safeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path.has_root_name())
        return false;
    for (const auto& part : path) {
        if (part == "..")
            return false;
    }
    return true;
}

bool identifier(const String& value)
{
    if (value.empty() || !(std::isalpha(static_cast<unsigned char>(value[0])) || value[0] == '_'))
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '-';
    });
}

bool validSha256(const String& value)
{
    if (value.size() != 71u || value.compare(0u, 7u, "sha256:") != 0)
        return false;
    return std::all_of(value.begin() + 7, value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

String platformsText(AotPlatform platforms)
{
    String result("[", &core::defaultCpuMemoryResource());
    bool comma = false;
    if (hasAotPlatform(platforms, AotPlatform::Windows)) {
        result += "windows";
        comma = true;
    }
    if (hasAotPlatform(platforms, AotPlatform::WebWasm)) {
        if (comma)
            result += ", ";
        result += "web-wasm";
        comma = true;
    }
    if (hasAotPlatform(platforms, AotPlatform::MacOS)) {
        if (comma)
            result += ", ";
        result += "macos";
    }
    result += "]";
    return result;
}

String depfileReference(
    const std::filesystem::path& path, const std::filesystem::path& workingDirectory)
{
    const auto absolute =
        path.is_absolute() ? path.lexically_normal() : (workingDirectory / path).lexically_normal();
    const auto relative = absolute.lexically_relative(workingDirectory);
    const auto text = relative.empty() ? absolute.generic_string() : relative.generic_string();
    String escaped(&core::defaultCpuMemoryResource());
    escaped.reserve(text.size());
    for (const char ch : text) {
        if (ch == ' ' || ch == '#' || ch == '$')
            escaped.push_back('\\');
        escaped.push_back(ch);
    }
    return escaped;
}

bool writeText(const std::filesystem::path& path, const String& value, ScriptDiagnostic& diagnostic)
{
    std::ifstream existing(path, std::ios::binary | std::ios::ate);
    if (existing) {
        const auto size = existing.tellg();
        if (size >= 0 && static_cast<std::uintmax_t>(size) == value.size()) {
            existing.seekg(0, std::ios::beg);
            char buffer[4096];
            std::size_t offset = 0u;
            bool equal = true;
            while (offset < value.size()) {
                const auto count = std::min(sizeof(buffer), value.size() - offset);
                existing.read(buffer, static_cast<std::streamsize>(count));
                if (existing.gcount() != static_cast<std::streamsize>(count)
                    || std::memcmp(buffer, value.data() + offset, count) != 0) {
                    equal = false;
                    break;
                }
                offset += count;
            }
            if (equal)
                return true;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        diagnostic = error("failed to open AOT package output", path);
        return false;
    }
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output.good()) {
        diagnostic = error("failed to write AOT package output", path);
        return false;
    }
    return true;
}

String packageIdentity(const ScriptAotPackageDefinition& package)
{
    String canonical(package.type, &core::defaultCpuMemoryResource());
    canonical += "\n";
    canonical += package.packageId;
    canonical += "\n";
    core::appendUnsigned(canonical, package.packageVersion);
    canonical += "\n";
    core::appendUnsigned(canonical, static_cast<std::uint32_t>(package.platforms));
    canonical += "\n";
    canonical += package.dependencyGraphHash;
    canonical += "\n";
    canonical += package.reflectionRegistryHash;
    canonical += "\n";
    canonical += package.hostContractHash;
    for (const auto& callable : package.callableContracts) {
        canonical += "\ncallable:";
        canonical += callable.bindingId;
        canonical += "\n";
        canonical += callable.targetId;
        canonical += "\n";
        canonical += callable.signatureHash;
        canonical += "\n";
        core::appendUnsigned(canonical, static_cast<std::uint32_t>(callable.origin));
        canonical += "\n";
        core::appendUnsigned(canonical, callable.fixedArity);
        canonical += "\n";
        canonical += callable.maySuspend ? "1" : "0";
        canonical += "\n";
        core::appendUnsigned(canonical, callable.lifetimeVersion);
    }
    for (const auto& module : package.modules) {
        canonical += "\n";
        canonical += module.moduleId;
        canonical += "\n";
        core::appendUnsigned(canonical, module.moduleVersion);
        canonical += "\n";
        canonical += pathText(module.source.lexically_relative(package.manifestPath.parent_path()));
        const auto source = core::readTextFile(module.source);
        if (source.success) {
            canonical += "\nsource:";
            canonical += sha256(source.text);
        }
        for (const auto& entry : module.entries) {
            canonical += "\nentry:";
            canonical += entry;
        }
        for (const auto& dependency : module.dependencies) {
            canonical += "\ndependency:";
            canonical += dependency;
        }
    }
    return sha256(canonical);
}

String moduleStem(const String& moduleId)
{
    String result(&core::defaultCpuMemoryResource());
    for (const unsigned char c : moduleId)
        result += std::isalnum(c) ? static_cast<char>(c) : '_';
    return result;
}

bool parsePlatforms(const YamlNode* node, AotPlatform& output)
{
    if (node == nullptr || !node->is_sequence())
        return false;
    output = AotPlatform::None;
    for (const auto& item : node->get_value_ref<const YamlNode::sequence_type&>()) {
        if (!item.is_string())
            return false;
        const auto value = stringValue(item);
        if (value == "windows")
            output = output | AotPlatform::Windows;
        else if (value == "web-wasm")
            output = output | AotPlatform::WebWasm;
        else if (value == "macos")
            output = output | AotPlatform::MacOS;
        else
            return false;
    }
    return output != AotPlatform::None;
}

bool parseBudget(const YamlNode* node, AotExecutionBudgetV5& budget)
{
    if (node == nullptr || !node->is_mapping())
        return false;
    std::uint64_t value = 0u;
    if (!unsignedValue(field(*node, "instructions"), budget.instructionLimit)
        || !unsignedValue(field(*node, "memory-bytes"), budget.maxMemoryBytes)
        || !unsignedValue(field(*node, "recursion"), value)
        || value > std::numeric_limits<std::uint32_t>::max())
        return false;
    budget.recursionLimit = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*node, "frame-values"), value)
        || value > std::numeric_limits<std::uint32_t>::max())
        return false;
    budget.maxFrameValues = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*node, "container-values"), value)
        || value > std::numeric_limits<std::uint32_t>::max())
        return false;
    budget.maxContainerValues = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*node, "call-arguments"), value)
        || value > std::numeric_limits<std::uint32_t>::max())
        return false;
    budget.maxCallArguments = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*node, "continuation-frames"), value)
        || value > std::numeric_limits<std::uint32_t>::max())
        return false;
    budget.maxContinuationFrames = static_cast<std::uint32_t>(value);
    return true;
}

bool hasClassDeclaration(const std::shared_ptr<Module>& module)
{
    return std::any_of(module->decls.begin(), module->decls.end(), [](const auto& declaration) {
        return declaration->kind == StmtKind::ClassDecl;
    });
}

bool sameTopLevelDeclaration(const StmtPtr& left, const StmtPtr& right)
{
    if (left->kind != right->kind)
        return false;
    if (left->kind == StmtKind::ImportDecl) {
        if (left->importModule != right->importModule
            || left->importIsModuleImport != right->importIsModuleImport
            || left->importAlias != right->importAlias
            || left->importNames.size() != right->importNames.size())
            return false;
        for (std::size_t index = 0; index < left->importNames.size(); ++index) {
            if (left->importNames[index].name != right->importNames[index].name
                || left->importNames[index].alias != right->importNames[index].alias)
                return false;
        }
        return true;
    }
    if (left->kind == StmtKind::ClassDecl)
        return left->className == right->className;
    if (left->kind == StmtKind::FuncDecl)
        return left->funcName == right->funcName;
    if (left->kind == StmtKind::VarDecl)
        return left->varName == right->varName;
    return false;
}

void appendUniqueDeclaration(core::pmr::Vector<StmtPtr>& declarations, const StmtPtr& declaration)
{
    if (std::none_of(
            declarations.begin(), declarations.end(), [&declaration](const auto& existing) {
                return sameTopLevelDeclaration(existing, declaration);
            }))
        declarations.push_back(declaration);
}

bool mergePackageClassDependencies(const ScriptAotPackageDefinition& package,
    const std::shared_ptr<Module>& syntax,
    core::pmr::Vector<String>& resolving,
    ScriptDiagnostic& diagnostic)
{
    core::pmr::Vector<StmtPtr> merged{&core::defaultCpuMemoryResource()};
    for (const auto& declaration : syntax->decls) {
        if (declaration->kind != StmtKind::ImportDecl || declaration->importModule.empty()) {
            appendUniqueDeclaration(merged, declaration);
            continue;
        }
        const auto dependency = std::find_if(
            package.modules.begin(), package.modules.end(), [&declaration](const auto& candidate) {
                return candidate.moduleId == declaration->importModule;
            });
        if (dependency == package.modules.end()) {
            appendUniqueDeclaration(merged, declaration);
            continue;
        }
        if (std::find(resolving.begin(), resolving.end(), declaration->importModule)
            != resolving.end()) {
            diagnostic = error("circular AOT source module import", dependency->source);
            return false;
        }
        const auto source = core::readTextFile(dependency->source);
        if (!source.success) {
            diagnostic = error(source.diagnostics.c_str(), dependency->source);
            return false;
        }
        auto parsed = parseSource(source.text, pathText(dependency->source));
        if (!parsed.ok() || !parsed.module) {
            diagnostic = parsed.ok()
                ? error("AOT dependency parser returned no module", dependency->source)
                : parsed.error;
            return false;
        }
        resolving.push_back(declaration->importModule);
        const bool nested =
            mergePackageClassDependencies(package, parsed.module, resolving, diagnostic);
        resolving.pop_back();
        if (!nested)
            return false;
        if (!hasClassDeclaration(parsed.module)) {
            appendUniqueDeclaration(merged, declaration);
            continue;
        }
        for (const auto& dependencyDeclaration : parsed.module->decls)
            appendUniqueDeclaration(merged, dependencyDeclaration);
    }
    syntax->decls = std::move(merged);
    return true;
}

ScriptFrontendResult compilePackageFrontend(const ScriptAotPackageDefinition& package,
    const ScriptAotPackageModuleInput& module,
    const String& source)
{
    ScriptFrontendResult result;
    auto parsed = parseSource(source, pathText(module.source));
    if (!parsed.ok() || !parsed.module) {
        result.error = parsed.ok() ? error("AOT package parser returned no module", module.source)
                                   : parsed.error;
        return result;
    }
    core::pmr::Vector<String> resolving{&core::defaultCpuMemoryResource()};
    resolving.push_back(module.moduleId);
    if (!mergePackageClassDependencies(package, parsed.module, resolving, result.error))
        return result;
    const auto analyzed = analyze(parsed.module, nullptr);
    if (!analyzed.ok()) {
        result.error = analyzed.error;
        return result;
    }
    result.module.syntax = std::move(parsed.module);
    result.module.moduleId = module.moduleId;
    return result;
}

} // namespace

ScriptAotPackageLoadResult ScriptAotPackageBuilder::load(const std::filesystem::path& manifestPath)
{
    ScriptAotPackageLoadResult result;
    result.package.manifestPath = std::filesystem::absolute(manifestPath).lexically_normal();
    const auto source = core::readTextFile(result.package.manifestPath);
    if (!source.success) {
        result.error = error(source.diagnostics.c_str(), result.package.manifestPath);
        return result;
    }
    YamlNode root;
    try {
        root = YamlNode::deserialize(source.text.data(), source.text.data() + source.text.size());
    } catch (const std::exception& exception) {
        result.error = error(exception.what(), result.package.manifestPath);
        return result;
    }
    const auto* type = field(root, "type");
    const auto* packageId = field(root, "package");
    const auto* version = field(root, "version");
    const auto* platforms = field(root, "platforms");
    const auto* budget = field(root, "budget");
    const auto* contracts = field(root, "contracts");
    const auto* modules = field(root, "modules");
    if (type == nullptr || !type->is_string() || packageId == nullptr || !packageId->is_string()
        || version == nullptr || !version->is_integer() || platforms == nullptr
        || !platforms->is_sequence() || budget == nullptr || !budget->is_mapping()
        || contracts == nullptr || !contracts->is_mapping() || modules == nullptr
        || !modules->is_sequence()) {
        result.error = error("AOT package YAML schema is incomplete", result.package.manifestPath);
        return result;
    }
    result.package.type = stringValue(*type);
    result.package.packageId = stringValue(*packageId);
    const auto packageVersion = version->get_value<YamlNode::integer_type>();
    if (packageVersion <= 0) {
        result.error = error("AOT package version must be positive", result.package.manifestPath);
        return result;
    }
    result.package.packageVersion = static_cast<std::uint32_t>(packageVersion);
    for (const auto& platform : platforms->get_value_ref<const YamlNode::sequence_type&>()) {
        if (!platform.is_string()) {
            result.error = error("AOT platform must be a string");
            return result;
        }
        const auto name = stringValue(platform);
        if (name == "windows")
            result.package.platforms = result.package.platforms | AotPlatform::Windows;
        else if (name == "web-wasm")
            result.package.platforms = result.package.platforms | AotPlatform::WebWasm;
        else if (name == "macos")
            result.package.platforms = result.package.platforms | AotPlatform::MacOS;
        else {
            result.error = error("AOT package platform is unsupported");
            return result;
        }
    }
    std::uint64_t value = 0u;
    if (!unsignedValue(field(*budget, "instructions"), result.package.budget.instructionLimit)
        || !unsignedValue(field(*budget, "memory-bytes"), result.package.budget.maxMemoryBytes)
        || !unsignedValue(field(*budget, "recursion"), value)) {
        result.error = error("AOT package budget is incomplete");
        return result;
    }
    result.package.budget.recursionLimit = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*budget, "frame-values"), value)) {
        result.error = error("AOT frame-values budget is missing");
        return result;
    }
    result.package.budget.maxFrameValues = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*budget, "container-values"), value)) {
        result.error = error("AOT container-values budget is missing");
        return result;
    }
    result.package.budget.maxContainerValues = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*budget, "call-arguments"), value)) {
        result.error = error("AOT call-arguments budget is missing");
        return result;
    }
    result.package.budget.maxCallArguments = static_cast<std::uint32_t>(value);
    if (!unsignedValue(field(*budget, "continuation-frames"), value)) {
        result.error = error("AOT continuation-frames budget is missing");
        return result;
    }
    result.package.budget.maxContinuationFrames = static_cast<std::uint32_t>(value);
    const auto* dependencyHash = field(*contracts, "dependency-graph-hash");
    const auto* reflectionHash = field(*contracts, "reflection-registry-hash");
    const auto* hostHash = field(*contracts, "host-contract-hash");
    if (dependencyHash == nullptr || !dependencyHash->is_string() || reflectionHash == nullptr
        || !reflectionHash->is_string() || hostHash == nullptr || !hostHash->is_string()) {
        result.error = error("AOT package contract hashes are required");
        return result;
    }
    result.package.dependencyGraphHash = stringValue(*dependencyHash);
    result.package.reflectionRegistryHash = stringValue(*reflectionHash);
    result.package.hostContractHash = stringValue(*hostHash);
    if (const auto* callables = field(*contracts, "callables")) {
        if (!callables->is_sequence()) {
            result.error = error("AOT callable contracts must be a sequence");
            return result;
        }
        for (const auto& node : callables->get_value_ref<const YamlNode::sequence_type&>()) {
            const auto* binding = field(node, "binding");
            const auto* target = field(node, "target");
            const auto* signature = field(node, "signature-hash");
            const auto* kind = field(node, "kind");
            const auto* arity = field(node, "arity");
            const auto* lifetime = field(node, "lifetime-version");
            if (!node.is_mapping() || binding == nullptr || !binding->is_string()
                || target == nullptr || !target->is_string() || signature == nullptr
                || !signature->is_string() || kind == nullptr || !kind->is_string()
                || arity == nullptr || !arity->is_integer() || lifetime == nullptr
                || !lifetime->is_integer()) {
                result.error = error("AOT callable contract is incomplete");
                return result;
            }
            ScriptAotCallableContractInput callable;
            callable.bindingId = stringValue(*binding);
            callable.targetId = stringValue(*target);
            callable.signatureHash = stringValue(*signature);
            const auto kindValue = stringValue(*kind);
            if (kindValue == "host")
                callable.origin = TypedIrCallableOrigin::HostCapability;
            else if (kindValue == "reflection")
                callable.origin = TypedIrCallableOrigin::ReflectionMember;
            else if (kindValue == "script")
                callable.origin = TypedIrCallableOrigin::ScriptFunction;
            else {
                result.error = error("AOT callable contract kind is unsupported");
                return result;
            }
            const auto arityValue = arity->get_value<YamlNode::integer_type>();
            const auto lifetimeValue = lifetime->get_value<YamlNode::integer_type>();
            if (arityValue < 0 || lifetimeValue <= 0
                || lifetimeValue > std::numeric_limits<std::uint32_t>::max()) {
                result.error = error("AOT callable contract numeric fields are invalid");
                return result;
            }
            callable.fixedArity = static_cast<int>(arityValue);
            callable.lifetimeVersion = static_cast<std::uint32_t>(lifetimeValue);
            if (const auto* maySuspend = field(node, "may-suspend")) {
                if (!maySuspend->is_boolean()) {
                    result.error = error("AOT callable may-suspend must be boolean");
                    return result;
                }
                callable.maySuspend = maySuspend->get_value<bool>();
            }
            result.package.callableContracts.push_back(std::move(callable));
        }
    }
    const auto base = result.package.manifestPath.parent_path();
    for (const auto& node : modules->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* id = field(node, "id");
        const auto* moduleVersion = field(node, "version");
        const auto* moduleSource = field(node, "source");
        if (id == nullptr || !id->is_string() || moduleVersion == nullptr
            || !moduleVersion->is_integer() || moduleSource == nullptr
            || !moduleSource->is_string()) {
            result.error = error("AOT package module schema is incomplete");
            return result;
        }
        ScriptAotPackageModuleInput module;
        module.moduleId = stringValue(*id);
        const auto versionValue = moduleVersion->get_value<YamlNode::integer_type>();
        if (versionValue <= 0) {
            result.error = error("AOT module version must be positive");
            return result;
        }
        module.moduleVersion = static_cast<std::uint32_t>(versionValue);
        const auto sourceText = stringValue(*moduleSource);
        const auto relativeSource = std::filesystem::path(sourceText.c_str());
        if (!safeRelativePath(relativeSource)) {
            result.error = error("AOT module source must be a safe relative path");
            return result;
        }
        module.source = (base / relativeSource).lexically_normal();
        if (!stringSequence(field(node, "entries"), module.entries, true)
            || !stringSequence(field(node, "dependencies"), module.dependencies, false)) {
            result.error = error("AOT module entries or dependencies are invalid");
            return result;
        }
        if (const auto* pluginDependencies = field(node, "plugin-dependencies")) {
            if (!pluginDependencies->is_sequence()) {
                result.error = error("AOT Plugin dependencies must be a sequence");
                return result;
            }
            for (const auto& dependencyNode :
                pluginDependencies->get_value_ref<const YamlNode::sequence_type&>()) {
                const auto* dependencyId = field(dependencyNode, "id");
                const auto* dependencyVersion = field(dependencyNode, "version");
                const auto* dependencyExport = field(dependencyNode, "export-contract-hash");
                const auto* dependencyReflection =
                    field(dependencyNode, "reflection-registry-hash");
                if (!dependencyNode.is_mapping() || dependencyId == nullptr
                    || !dependencyId->is_string() || dependencyVersion == nullptr
                    || !dependencyVersion->is_integer() || dependencyExport == nullptr
                    || !dependencyExport->is_string() || dependencyReflection == nullptr
                    || !dependencyReflection->is_string()) {
                    result.error = error("AOT Plugin dependency schema is incomplete");
                    return result;
                }
                const auto dependencyVersionValue =
                    dependencyVersion->get_value<YamlNode::integer_type>();
                if (dependencyVersionValue <= 0) {
                    result.error = error("AOT Plugin dependency version must be positive");
                    return result;
                }
                AotPluginDependencyManifestV5 dependency;
                dependency.moduleId = stringValue(*dependencyId);
                dependency.moduleVersion = static_cast<std::uint32_t>(dependencyVersionValue);
                dependency.exportContractHash = stringValue(*dependencyExport);
                dependency.reflectionRegistryHash = stringValue(*dependencyReflection);
                if (const auto* services = field(dependencyNode, "services")) {
                    if (!services->is_sequence()) {
                        result.error = error("AOT Plugin services must be a sequence");
                        return result;
                    }
                    for (const auto& serviceNode :
                        services->get_value_ref<const YamlNode::sequence_type&>()) {
                        const auto* serviceId = field(serviceNode, "id");
                        const auto* serviceVersion = field(serviceNode, "version");
                        if (!serviceNode.is_mapping() || serviceId == nullptr
                            || !serviceId->is_string() || serviceVersion == nullptr
                            || !serviceVersion->is_integer()) {
                            result.error =
                                error("AOT Plugin service dependency schema is incomplete");
                            return result;
                        }
                        const auto serviceVersionValue =
                            serviceVersion->get_value<YamlNode::integer_type>();
                        if (serviceVersionValue <= 0) {
                            result.error =
                                error("AOT Plugin service dependency version must be positive");
                            return result;
                        }
                        dependency.services.push_back(
                            AotServiceDependencyManifestV5{stringValue(*serviceId),
                                static_cast<std::uint32_t>(serviceVersionValue)});
                    }
                }
                module.pluginDependencies.push_back(std::move(dependency));
            }
        }
        result.package.modules.push_back(std::move(module));
    }
    const auto validation = validate(result.package);
    if (!validation.ok())
        result.error = validation.diagnostic();
    return result;
}

ScriptResult ScriptAotPackageBuilder::validate(const ScriptAotPackageDefinition& package)
{
    if (package.type != kAotPackageManifestType || !identifier(package.packageId)
        || package.packageVersion == 0u || package.platforms == AotPlatform::None
        || package.modules.empty() || !validSha256(package.dependencyGraphHash)
        || !validSha256(package.reflectionRegistryHash) || !validSha256(package.hostContractHash)) {
        return ScriptResult::failure(error("AOT package identity is invalid"));
    }
    const auto expectedDependencyHash = contractHash(package, ContractHashKind::DependencyGraph);
    if (package.dependencyGraphHash != expectedDependencyHash) {
        return contractHashMismatch(
            "dependency-graph-hash", expectedDependencyHash, package.dependencyGraphHash);
    }
    const auto expectedReflectionHash = contractHash(package, ContractHashKind::ReflectionRegistry);
    if (package.reflectionRegistryHash != expectedReflectionHash) {
        return contractHashMismatch(
            "reflection-registry-hash", expectedReflectionHash, package.reflectionRegistryHash);
    }
    const auto expectedHostHash = contractHash(package, ContractHashKind::HostContract);
    if (package.hostContractHash != expectedHostHash) {
        return contractHashMismatch(
            "host-contract-hash", expectedHostHash, package.hostContractHash);
    }
    for (std::size_t index = 0; index < package.modules.size(); ++index) {
        const auto& module = package.modules[index];
        if (!identifier(module.moduleId) || module.moduleVersion == 0u || module.source.empty()
            || module.entries.empty() || !std::filesystem::is_regular_file(module.source)) {
            return ScriptResult::failure(error("AOT package module is invalid", module.source));
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (package.modules[other].moduleId == module.moduleId)
                return ScriptResult::failure(error("AOT package module ids are duplicated"));
        }
        for (const auto& dependency : module.dependencies) {
            const auto found = std::find_if(package.modules.begin(),
                package.modules.end(),
                [&dependency](const auto& candidate) {
                    return candidate.moduleId == dependency;
                });
            if (found == package.modules.end())
                return ScriptResult::failure(error("AOT package dependency is not closed"));
        }
        for (std::size_t pluginIndex = 0; pluginIndex < module.pluginDependencies.size();
            ++pluginIndex) {
            const auto& dependency = module.pluginDependencies[pluginIndex];
            if (dependency.moduleId.empty() || dependency.moduleVersion == 0u
                || !validSha256(dependency.exportContractHash)
                || !validSha256(dependency.reflectionRegistryHash)) {
                return ScriptResult::failure(error("AOT Plugin dependency contract is invalid"));
            }
            for (std::size_t serviceIndex = 0; serviceIndex < dependency.services.size();
                ++serviceIndex) {
                const auto& service = dependency.services[serviceIndex];
                if (service.serviceId.empty() || service.version == 0u) {
                    return ScriptResult::failure(error("AOT Plugin service dependency is invalid"));
                }
                for (std::size_t other = 0; other < serviceIndex; ++other) {
                    if (dependency.services[other].serviceId == service.serviceId) {
                        return ScriptResult::failure(
                            error("AOT Plugin service dependency is duplicated"));
                    }
                }
            }
            for (std::size_t other = 0; other < pluginIndex; ++other) {
                if (module.pluginDependencies[other].moduleId == dependency.moduleId) {
                    return ScriptResult::failure(error("AOT Plugin dependency is duplicated"));
                }
            }
        }
    }
    for (std::size_t index = 0; index < package.callableContracts.size(); ++index) {
        const auto& callable = package.callableContracts[index];
        if (callable.bindingId.empty() || callable.targetId.empty()
            || !validSha256(callable.signatureHash) || callable.fixedArity < 0
            || callable.lifetimeVersion == 0u) {
            return ScriptResult::failure(error("AOT callable contract is invalid"));
        }
        for (std::size_t other = 0; other < index; ++other) {
            if (package.callableContracts[other].bindingId == callable.bindingId
                || package.callableContracts[other].targetId == callable.targetId) {
                return ScriptResult::failure(error("AOT callable contract identity is duplicated"));
            }
        }
    }
    return ScriptResult::success();
}

DirectNativeGeneratedModuleV5 ScriptAotCompiler::compileModule(
    const ScriptAotPackageDefinition& package,
    const ScriptAotPackageModuleInput& module,
    const String& identity,
    const String& generatedHeader) const
{
    DirectNativeGeneratedModuleV5 result;
    const auto source = core::readTextFile(module.source);
    if (!source.success) {
        result.error = error(source.diagnostics.c_str(), module.source);
        return result;
    }
    auto frontend = compilePackageFrontend(package, module, source.text);
    if (!frontend.ok()) {
        result.error = frontend.error;
        return result;
    }
    for (const auto& input : package.callableContracts) {
        TypedIrCallableBinding binding;
        binding.bindingId = input.bindingId;
        binding.targetId = input.targetId;
        binding.signatureHash = input.signatureHash;
        binding.origin = input.origin;
        binding.fixedArity = input.fixedArity;
        binding.maySuspend = input.maySuspend;
        binding.lifetimeVersion = input.lifetimeVersion;
        frontend.module.callableBindings.push_back(std::move(binding));
    }
    core::pmr::Set<String> externalModules{&core::defaultCpuMemoryResource()};
    for (const auto& input : package.callableContracts) {
        const auto separator = input.targetId.find('.');
        if (separator != String::npos)
            externalModules.emplace(input.targetId.substr(0u, separator));
    }
    for (const auto& externalModule : externalModules) {
        TypedIrCallableBinding marker;
        marker.bindingId = "arp-contract-module:";
        marker.bindingId += externalModule;
        marker.targetId = externalModule;
        marker.targetId += ".*";
        marker.signatureHash = package.hostContractHash;
        marker.fixedArity = 0;
        marker.lifetimeVersion = package.packageVersion;
        frontend.module.callableBindings.push_back(std::move(marker));
    }
    for (const auto& dependency : module.dependencies) {
        const auto dependencyInput = std::find_if(
            package.modules.begin(), package.modules.end(), [&dependency](const auto& candidate) {
                return candidate.moduleId == dependency;
            });
        if (dependencyInput == package.modules.end()) {
            result.error = error("AOT package dependency is not closed", module.source);
            return result;
        }
        const auto dependencySource = core::readTextFile(dependencyInput->source);
        if (!dependencySource.success) {
            result.error = error(dependencySource.diagnostics.c_str(), dependencyInput->source);
            return result;
        }
        auto dependencyFrontend =
            compilePackageFrontend(package, *dependencyInput, dependencySource.text);
        if (!dependencyFrontend.ok()) {
            result.error = dependencyFrontend.error;
            return result;
        }
        const auto dependencyIr = ScriptLowerer::lower(dependencyFrontend.module);
        if (!dependencyIr.ok()) {
            result.error = dependencyIr.error;
            return result;
        }
        if (!dependencyIr.module.scriptClassSchemas.empty())
            continue;
        for (const auto& function : dependencyIr.module.functions) {
            TypedIrCallableBinding binding;
            binding.bindingId = "arp-script:";
            binding.bindingId += package.packageId;
            binding.bindingId += "/";
            binding.bindingId += dependencyInput->moduleId;
            binding.bindingId += "/";
            binding.bindingId += function.name;
            binding.bindingId += "/v";
            core::appendUnsigned(binding.bindingId, dependencyInput->moduleVersion);
            binding.origin = TypedIrCallableOrigin::ScriptFunction;
            binding.targetId = function.id;
            binding.signatureHash = typedIrSignatureHash(function.signature);
            binding.fixedArity = function.parameterCount;
            binding.maySuspend = !function.safepoints.empty();
            binding.generatorBridge = function.isGenerator;
            binding.lifetimeVersion = dependencyInput->moduleVersion;
            frontend.module.callableBindings.push_back(std::move(binding));
        }
    }
    auto lowered = ScriptLowerer::lower(frontend.module);
    if (!lowered.ok()) {
        result.error = lowered.error;
        return result;
    }
    for (const auto& dependency : lowered.module.dependencies) {
        const auto packageModule = std::find_if(
            package.modules.begin(), package.modules.end(), [&dependency](const auto& candidate) {
                return candidate.moduleId == dependency;
            });
        if (packageModule != package.modules.end()) {
            if (std::find(module.dependencies.begin(), module.dependencies.end(), dependency)
                == module.dependencies.end()) {
                result.error =
                    error("AOT source import is missing from module dependencies", module.source);
                return result;
            }
            continue;
        }
        const String prefix = dependency + ".";
        const bool contracted = std::any_of(package.callableContracts.begin(),
            package.callableContracts.end(),
            [&prefix](const auto& callable) {
                return callable.targetId.rfind(prefix, 0u) == 0u;
            });
        if (!contracted) {
            result.error =
                error("AOT external import has no declared callable contract", module.source);
            return result;
        }
    }
    for (const auto& dependency : module.dependencies) {
        if (std::find(
                lowered.module.dependencies.begin(), lowered.module.dependencies.end(), dependency)
            == lowered.module.dependencies.end()) {
            const auto dependencyInput = std::find_if(package.modules.begin(),
                package.modules.end(),
                [&dependency](const auto& candidate) {
                    return candidate.moduleId == dependency;
                });
            if (dependencyInput != package.modules.end()) {
                const auto dependencySource = core::readTextFile(dependencyInput->source);
                if (dependencySource.success) {
                    const auto parsed =
                        parseSource(dependencySource.text, pathText(dependencyInput->source));
                    if (parsed.ok() && parsed.module && hasClassDeclaration(parsed.module))
                        continue;
                }
            }
            result.error =
                error("AOT declared dependency is not imported by the source", module.source);
            return result;
        }
    }
    lowered.module.moduleVersion = module.moduleVersion;
    lowered.module.fingerprint = computeTypedIrFingerprint(lowered.module);
    for (const auto& entry : module.entries) {
        const auto found = std::find_if(lowered.module.functions.begin(),
            lowered.module.functions.end(),
            [&entry](const auto& function) {
                return function.name == entry || function.id == entry;
            });
        if (found == lowered.module.functions.end()) {
            result.error = error("AOT package declared entry is missing", module.source);
            return result;
        }
    }
    DirectNativeCodegenOptionsV5 options;
    options.packageId = package.packageId;
    options.packageVersion = package.packageVersion;
    options.packageIdentity = identity;
    options.sourceHash = sha256(source.text);
    options.dependencyGraphHash = package.dependencyGraphHash;
    options.reflectionRegistryHash = package.reflectionRegistryHash;
    options.hostContractHash = package.hostContractHash;
    options.generatedHeader = generatedHeader;
    const auto stem = moduleStem(module.moduleId);
    options.registrationSymbol = "arp_register_script_aot_package_v5_";
    options.registrationSymbol += stem;
    options.moduleDescriptorSymbol = "arp_aot_v5_module_descriptor_";
    options.moduleDescriptorSymbol += stem;
    options.platforms = package.platforms;
    options.budget = package.budget;
    options.pluginDependencies = module.pluginDependencies;
    return generateDirectNativeModuleV5(lowered.module, options);
}

ScriptAotPackageBuildResult ScriptAotPackageBuilder::build(
    const ScriptAotPackageDefinition& package, const std::filesystem::path& outputDirectory) const
{
    ScriptAotPackageBuildResult result;
    const auto validation = validate(package);
    if (!validation.ok()) {
        result.error = validation.diagnostic();
        return result;
    }
    std::error_code ec;
    std::filesystem::create_directories(outputDirectory, ec);
    if (ec) {
        result.error = error("failed to create AOT output directory", outputDirectory);
        return result;
    }
    result.packageIdentity = packageIdentity(package);
    ScriptAotCompiler compiler;
    String manifest("type: arp.script.aot-package/v1\ncontract-version: 5\npackage: ",
        &core::defaultCpuMemoryResource());
    manifest += package.packageId;
    manifest += "\npackage-version: ";
    core::appendUnsigned(manifest, package.packageVersion);
    manifest += "\npackage-identity: ";
    manifest += result.packageIdentity;
    manifest += "\nplatforms: ";
    manifest += platformsText(package.platforms);
    manifest += "\nbudget:\n  instructions: ";
    core::appendUnsigned(manifest, package.budget.instructionLimit);
    manifest += "\n  recursion: ";
    core::appendUnsigned(manifest, package.budget.recursionLimit);
    manifest += "\n  frame-values: ";
    core::appendUnsigned(manifest, package.budget.maxFrameValues);
    manifest += "\n  container-values: ";
    core::appendUnsigned(manifest, package.budget.maxContainerValues);
    manifest += "\n  call-arguments: ";
    core::appendUnsigned(manifest, package.budget.maxCallArguments);
    manifest += "\n  continuation-frames: ";
    core::appendUnsigned(manifest, package.budget.maxContinuationFrames);
    manifest += "\n  memory-bytes: ";
    core::appendUnsigned(manifest, package.budget.maxMemoryBytes);
    manifest += "\nmodules:\n";
    const auto registryPath = outputDirectory / "package_registry.gen.cpp";
    std::error_code workingDirectoryError;
    const auto workingDirectory =
        std::filesystem::current_path(workingDirectoryError).lexically_normal();
    if (workingDirectoryError) {
        result.error =
            error("failed to resolve AOT package build working directory", outputDirectory);
        return result;
    }
    String depfile(&core::defaultCpuMemoryResource());
    depfile += depfileReference(registryPath, workingDirectory);
    depfile += ": ";
    depfile += depfileReference(package.manifestPath, workingDirectory);
    String registry("#include \"scriptvm/aot/aot_v5.h\"\n#include <array>\n",
        &core::defaultCpuMemoryResource());
    core::pmr::Vector<String> descriptorSymbols{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AotPackageManifestModuleV5> generatedModules{
        &core::defaultCpuMemoryResource()};
    result.report = "AOT package: ";
    result.report += package.packageId;
    result.report += "\n";
    for (const auto& module : package.modules) {
        const auto stem = moduleStem(module.moduleId);
        String headerName(stem, &core::defaultCpuMemoryResource());
        headerName += ".gen.h";
        String sourceName(stem, &core::defaultCpuMemoryResource());
        sourceName += ".gen.cpp";
        auto generated =
            compiler.compileModule(package, module, result.packageIdentity, headerName);
        if (!generated.ok()) {
            result.error = generated.error;
            result.report += generated.error.message;
            return result;
        }
        const auto headerPath = outputDirectory / std::filesystem::path(headerName.c_str());
        const auto sourcePath = outputDirectory / std::filesystem::path(sourceName.c_str());
        if (!writeText(headerPath, generated.header, result.error)
            || !writeText(sourcePath, generated.source, result.error))
            return result;
        result.outputs.push_back(headerPath);
        result.outputs.push_back(sourcePath);
        depfile += " ";
        depfile += depfileReference(module.source, workingDirectory);
        registry += "#include ";
        registry += String("\"", &core::defaultCpuMemoryResource());
        registry += headerName;
        registry += "\"\n";
        String descriptor("arp_aot_v5_module_descriptor_", &core::defaultCpuMemoryResource());
        descriptor += stem;
        descriptorSymbols.push_back(descriptor);
        const auto& generatedModule = generated.manifest.modules[0];
        generatedModules.push_back(generatedModule);
        manifest += "  - id: ";
        manifest += module.moduleId;
        manifest += "\n    version: ";
        core::appendUnsigned(manifest, module.moduleVersion);
        manifest += "\n    artifact-identity: ";
        manifest += generatedModule.artifactIdentity;
        manifest += "\n    export-contract-hash: ";
        manifest += generatedModule.exportContractHash;
        manifest += "\n    source-hash: ";
        manifest += generatedModule.sourceHash;
        manifest += "\n    dependency-graph-hash: ";
        manifest += generatedModule.dependencyGraphHash;
        manifest += "\n    reflection-registry-hash: ";
        manifest += generatedModule.reflectionRegistryHash;
        manifest += "\n    host-contract-hash: ";
        manifest += generatedModule.hostContractHash;
        manifest += "\n    semantic-abi-version: ";
        core::appendUnsigned(manifest, generatedModule.semanticAbiVersion);
        manifest += "\n    entry-abi-version: ";
        core::appendUnsigned(manifest, generatedModule.entryAbiVersion);
        manifest += "\n    platforms: ";
        manifest += platformsText(generatedModule.platforms);
        manifest += "\n    global-count: ";
        core::appendUnsigned(manifest, generatedModule.globalCount);
        manifest += "\n    module-init: ";
        manifest += generatedModule.moduleInitFunctionId.empty()
            ? "none"
            : generatedModule.moduleInitFunctionId;
        manifest += "\n    budget:\n      instructions: ";
        core::appendUnsigned(manifest, generatedModule.budget.instructionLimit);
        manifest += "\n      recursion: ";
        core::appendUnsigned(manifest, generatedModule.budget.recursionLimit);
        manifest += "\n      frame-values: ";
        core::appendUnsigned(manifest, generatedModule.budget.maxFrameValues);
        manifest += "\n      container-values: ";
        core::appendUnsigned(manifest, generatedModule.budget.maxContainerValues);
        manifest += "\n      call-arguments: ";
        core::appendUnsigned(manifest, generatedModule.budget.maxCallArguments);
        manifest += "\n      continuation-frames: ";
        core::appendUnsigned(manifest, generatedModule.budget.maxContinuationFrames);
        manifest += "\n      memory-bytes: ";
        core::appendUnsigned(manifest, generatedModule.budget.maxMemoryBytes);
        if (!generatedModule.pluginDependencies.empty()) {
            manifest += "\n    plugin-dependencies:\n";
            for (const auto& dependency : generatedModule.pluginDependencies) {
                manifest += "      - id: ";
                manifest += dependency.moduleId;
                manifest += "\n        version: ";
                core::appendUnsigned(manifest, dependency.moduleVersion);
                manifest += "\n        export-contract-hash: ";
                manifest += dependency.exportContractHash;
                manifest += "\n        reflection-registry-hash: ";
                manifest += dependency.reflectionRegistryHash;
                manifest += "\n";
                if (!dependency.services.empty()) {
                    manifest += "\n        services:\n";
                    for (const auto& service : dependency.services) {
                        manifest += "          - id: ";
                        manifest += service.serviceId;
                        manifest += "\n            version: ";
                        core::appendUnsigned(manifest, service.version);
                        manifest += "\n";
                    }
                }
            }
        }
        manifest += "\n    functions:\n";
        for (const auto& function : generatedModule.functions) {
            manifest += "      - id: ";
            manifest += function.functionId;
            manifest += "\n        identity: ";
            manifest += function.functionIdentity;
            manifest += "\n        signature-hash: ";
            manifest += function.signatureHash;
            manifest += "\n        entry: ";
            manifest += function.entrySymbol;
            manifest += "\n        entry-abi-version: ";
            core::appendUnsigned(manifest, function.entryAbiVersion);
            manifest += "\n        execution-kind: DirectNative";
            manifest += "\n        osr: function-boundary,operation-boundary";
            if ((static_cast<std::uint32_t>(function.osrCapability)
                    & static_cast<std::uint32_t>(AotOsrCapability::Resumable))
                != 0u) {
                manifest += ",resumable";
            }
            manifest += "\n";
        }
        result.report += generated.report;
    }
    registry += "\nnamespace {\nusing namespace arp::scriptvm;\n";
    for (std::size_t moduleIndex = 0; moduleIndex < package.modules.size(); ++moduleIndex) {
        const auto& module = package.modules[moduleIndex];
        if (module.dependencies.empty())
            continue;
        registry += "const std::array<AotModuleDependencyV5, ";
        core::appendUnsigned(registry, module.dependencies.size());
        registry += "> dependencies_";
        core::appendUnsigned(registry, moduleIndex);
        registry += " = {{";
        for (const auto& dependencyId : module.dependencies) {
            const auto dependency = std::find_if(package.modules.begin(),
                package.modules.end(),
                [&dependencyId](const auto& candidate) {
                    return candidate.moduleId == dependencyId;
                });
            const auto dependencyIndex =
                static_cast<std::size_t>(std::distance(package.modules.begin(), dependency));
            const auto& generatedDependency = generatedModules[dependencyIndex];
            registry += "{\"";
            registry += dependency->moduleId;
            registry += "\", ";
            core::appendUnsigned(registry, dependency->moduleVersion);
            registry += "u, \"";
            registry += generatedDependency.artifactIdentity;
            registry += "\", \"";
            registry += generatedDependency.exportContractHash;
            registry += "\"},";
        }
        registry += "}};\n";
    }
    registry += "std::array<AotModuleDescriptorV5, ";
    core::appendUnsigned(registry, descriptorSymbols.size());
    registry += "> makeModules() { std::array<AotModuleDescriptorV5, ";
    core::appendUnsigned(registry, descriptorSymbols.size());
    registry += "> result = {";
    for (const auto& symbol : descriptorSymbols) {
        registry += "*";
        registry += symbol;
        registry += "(),";
    }
    registry += "};";
    for (std::size_t moduleIndex = 0; moduleIndex < package.modules.size(); ++moduleIndex) {
        if (package.modules[moduleIndex].dependencies.empty())
            continue;
        registry += " result[";
        core::appendUnsigned(registry, moduleIndex);
        registry += "].dependencies = dependencies_";
        core::appendUnsigned(registry, moduleIndex);
        registry += ".data(); result[";
        core::appendUnsigned(registry, moduleIndex);
        registry += "].dependencyCount = dependencies_";
        core::appendUnsigned(registry, moduleIndex);
        registry += ".size();";
    }
    registry +=
        " return result; }\nconst auto modules = makeModules();\nconst AotPackageDescriptorV5 package = {kAotContractVersion, kAotPackageManifestType, \"";
    registry += package.packageId;
    registry += "\", ";
    core::appendUnsigned(registry, package.packageVersion);
    registry += "u, \"";
    registry += result.packageIdentity;
    registry += "\", \"";
    const auto manifestHash = sha256(manifest);
    registry += manifestHash;
    registry += "\", static_cast<AotPlatform>(";
    core::appendUnsigned(registry, static_cast<std::uint32_t>(package.platforms));
    registry +=
        "u), modules.data(), modules.size()};\n}\n#if defined(_WIN32) || defined(__CYGWIN__)\n#define ARP_AOT_PACKAGE_EXPORT __declspec(dllexport)\n#else\n#define ARP_AOT_PACKAGE_EXPORT __attribute__((visibility(\"default\")))\n#endif\nextern \"C\" ARP_AOT_PACKAGE_EXPORT int arp_register_script_aot_package_v5(AotRegistrationHostV5* host) { if (host == nullptr || host->contractVersion != kAotContractVersion || host->registerPackage == nullptr) return 0; return host->registerPackage(&package, host->userData) ? 1 : 0; }\n";
    manifest += "manifest-hash: ";
    manifest += manifestHash;
    manifest += "\n";
    const auto manifestPath = outputDirectory / "package.aot-v5.yaml";
    const auto depfilePath = outputDirectory / "package.d";
    const auto reportPath = outputDirectory / "package.report.txt";
    depfile += "\n";
    AotPackageManifestV5 parsedManifest;
    const auto hostPlatform = currentAotPlatform();
    const auto validationPlatform = hasAotPlatform(package.platforms, hostPlatform)
        ? hostPlatform
        : (hasAotPlatform(package.platforms, AotPlatform::Windows)
                  ? AotPlatform::Windows
                  : (hasAotPlatform(package.platforms, AotPlatform::WebWasm) ? AotPlatform::WebWasm
                                                                             : AotPlatform::MacOS));
    const auto parsed = parseAotPackageManifestV5(manifest, parsedManifest, validationPlatform);
    if (!parsed.ok()) {
        result.error = parsed.diagnostic();
        return result;
    }
    if (!writeText(registryPath, registry, result.error)
        || !writeText(manifestPath, manifest, result.error)
        || !writeText(depfilePath, depfile, result.error)
        || !writeText(reportPath, result.report, result.error))
        return result;
    result.outputs.push_back(registryPath);
    result.outputs.push_back(manifestPath);
    result.outputs.push_back(depfilePath);
    result.outputs.push_back(reportPath);
    return result;
}

} // namespace arp::scriptvm

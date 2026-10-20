#include "scriptvm/aot/aot_manifest_runtime.h"

#include <cstdint>
#include <exception>
#include <fkYAML/node.hpp>
#include <limits>

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

ScriptDiagnostic error(const char* message)
{
    return ScriptDiagnostic{ScriptDiagnosticCode::AotArtifactInvalid,
        String(message, &core::defaultCpuMemoryResource())};
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

} // namespace

ScriptResult parseAotPackageManifestV5(
    const String& yaml, AotPackageManifestV5& output, AotPlatform requiredPlatform)
{
    output = {};
    YamlNode root;
    try {
        root = YamlNode::deserialize(yaml.data(), yaml.data() + yaml.size());
    } catch (const std::exception& exception) {
        return ScriptResult::failure(error(exception.what()));
    }
    const auto* type = field(root, "type");
    const auto* version = field(root, "contract-version");
    const auto* packageId = field(root, "package");
    const auto* packageVersion = field(root, "package-version");
    const auto* identity = field(root, "package-identity");
    const auto* manifestHash = field(root, "manifest-hash");
    const auto* modules = field(root, "modules");
    if (type == nullptr || !type->is_string() || version == nullptr || !version->is_integer()
        || packageId == nullptr || !packageId->is_string() || packageVersion == nullptr
        || !packageVersion->is_integer() || identity == nullptr || !identity->is_string()
        || manifestHash == nullptr || !manifestHash->is_string() || modules == nullptr
        || !modules->is_sequence()) {
        return ScriptResult::failure(error("AOT v5 artifact manifest schema is incomplete"));
    }
    const auto contract = version->get_value<YamlNode::integer_type>();
    if (contract != static_cast<YamlNode::integer_type>(kAotContractVersion)) {
        return ScriptResult::failure(ScriptDiagnostic{ScriptDiagnosticCode::AotVersionMismatch,
            String(
                "strict AOT artifact parser accepts v5 only", &core::defaultCpuMemoryResource())});
    }
    output.type = stringValue(*type);
    output.contractVersion = static_cast<std::uint32_t>(contract);
    output.packageId = stringValue(*packageId);
    const auto packageVersionValue = packageVersion->get_value<YamlNode::integer_type>();
    if (packageVersionValue <= 0
        || packageVersionValue > std::numeric_limits<std::uint32_t>::max()) {
        return ScriptResult::failure(error("AOT v5 package version is invalid"));
    }
    output.packageVersion = static_cast<std::uint32_t>(packageVersionValue);
    output.packageIdentity = stringValue(*identity);
    output.manifestHash = stringValue(*manifestHash);
    if (!parsePlatforms(field(root, "platforms"), output.platforms))
        return ScriptResult::failure(error("AOT v5 package platforms are invalid"));

    for (const auto& moduleNode : modules->get_value_ref<const YamlNode::sequence_type&>()) {
        if (!moduleNode.is_mapping())
            return ScriptResult::failure(error("AOT v5 module manifest must be a mapping"));
        AotPackageManifestModuleV5 module;
        const auto* moduleId = field(moduleNode, "id");
        const auto* moduleVersion = field(moduleNode, "version");
        const auto* artifact = field(moduleNode, "artifact-identity");
        const auto* exportContract = field(moduleNode, "export-contract-hash");
        const auto* sourceHash = field(moduleNode, "source-hash");
        const auto* dependencyHash = field(moduleNode, "dependency-graph-hash");
        const auto* reflectionHash = field(moduleNode, "reflection-registry-hash");
        const auto* hostHash = field(moduleNode, "host-contract-hash");
        const auto* semanticAbi = field(moduleNode, "semantic-abi-version");
        const auto* entryAbi = field(moduleNode, "entry-abi-version");
        const auto* globalCount = field(moduleNode, "global-count");
        const auto* moduleInit = field(moduleNode, "module-init");
        const auto* functions = field(moduleNode, "functions");
        if (moduleId == nullptr || !moduleId->is_string() || moduleVersion == nullptr
            || !moduleVersion->is_integer() || artifact == nullptr || !artifact->is_string()
            || exportContract == nullptr || !exportContract->is_string() || sourceHash == nullptr
            || !sourceHash->is_string() || dependencyHash == nullptr || !dependencyHash->is_string()
            || reflectionHash == nullptr || !reflectionHash->is_string() || hostHash == nullptr
            || !hostHash->is_string() || semanticAbi == nullptr || !semanticAbi->is_integer()
            || entryAbi == nullptr || !entryAbi->is_integer() || globalCount == nullptr
            || !globalCount->is_integer() || moduleInit == nullptr || !moduleInit->is_string()
            || functions == nullptr || !functions->is_sequence()) {
            return ScriptResult::failure(error("AOT v5 module manifest schema is incomplete"));
        }
        module.moduleId = stringValue(*moduleId);
        const auto moduleVersionValue = moduleVersion->get_value<YamlNode::integer_type>();
        const auto semanticAbiValue = semanticAbi->get_value<YamlNode::integer_type>();
        const auto entryAbiValue = entryAbi->get_value<YamlNode::integer_type>();
        const auto globalCountValue = globalCount->get_value<YamlNode::integer_type>();
        if (moduleVersionValue <= 0
            || moduleVersionValue > std::numeric_limits<std::uint32_t>::max()
            || semanticAbiValue <= 0 || semanticAbiValue > std::numeric_limits<std::uint32_t>::max()
            || entryAbiValue <= 0 || entryAbiValue > std::numeric_limits<std::uint32_t>::max()
            || globalCountValue < 0) {
            return ScriptResult::failure(error("AOT v5 module ABI or version is invalid"));
        }
        module.moduleVersion = static_cast<std::uint32_t>(moduleVersionValue);
        module.semanticAbiVersion = static_cast<std::uint32_t>(semanticAbiValue);
        module.entryAbiVersion = static_cast<std::uint32_t>(entryAbiValue);
        module.globalCount = static_cast<std::size_t>(globalCountValue);
        const auto moduleInitValue = stringValue(*moduleInit);
        if (moduleInitValue != "none")
            module.moduleInitFunctionId = moduleInitValue;
        module.artifactIdentity = stringValue(*artifact);
        module.exportContractHash = stringValue(*exportContract);
        module.sourceHash = stringValue(*sourceHash);
        module.dependencyGraphHash = stringValue(*dependencyHash);
        module.reflectionRegistryHash = stringValue(*reflectionHash);
        module.hostContractHash = stringValue(*hostHash);
        if (!parsePlatforms(field(moduleNode, "platforms"), module.platforms)
            || !parseBudget(field(moduleNode, "budget"), module.budget)) {
            return ScriptResult::failure(error("AOT v5 module platform or budget is invalid"));
        }
        if (const auto* pluginDependencies = field(moduleNode, "plugin-dependencies")) {
            if (!pluginDependencies->is_sequence()) {
                return ScriptResult::failure(
                    error("AOT v5 Plugin dependencies must be a sequence"));
            }
            for (const auto& dependencyNode :
                pluginDependencies->get_value_ref<const YamlNode::sequence_type&>()) {
                if (!dependencyNode.is_mapping()) {
                    return ScriptResult::failure(
                        error("AOT v5 Plugin dependency schema is incomplete"));
                }
                const auto* dependencyId = field(dependencyNode, "id");
                const auto* dependencyVersion = field(dependencyNode, "version");
                const auto* dependencyExport = field(dependencyNode, "export-contract-hash");
                const auto* dependencyReflection =
                    field(dependencyNode, "reflection-registry-hash");
                if (dependencyId == nullptr || !dependencyId->is_string()
                    || dependencyVersion == nullptr || !dependencyVersion->is_integer()
                    || dependencyExport == nullptr || !dependencyExport->is_string()
                    || dependencyReflection == nullptr || !dependencyReflection->is_string()) {
                    return ScriptResult::failure(
                        error("AOT v5 Plugin dependency schema is incomplete"));
                }
                const auto versionValue = dependencyVersion->get_value<YamlNode::integer_type>();
                if (versionValue <= 0 || versionValue > std::numeric_limits<std::uint32_t>::max()) {
                    return ScriptResult::failure(
                        error("AOT v5 Plugin dependency version is invalid"));
                }
                AotPluginDependencyManifestV5 dependency;
                dependency.moduleId = stringValue(*dependencyId);
                dependency.moduleVersion = static_cast<std::uint32_t>(versionValue);
                dependency.exportContractHash = stringValue(*dependencyExport);
                dependency.reflectionRegistryHash = stringValue(*dependencyReflection);
                if (const auto* services = field(dependencyNode, "services")) {
                    if (!services->is_sequence()) {
                        return ScriptResult::failure(
                            error("AOT Plugin services must be a sequence"));
                    }
                    for (const auto& serviceNode :
                        services->get_value_ref<const YamlNode::sequence_type&>()) {
                        if (!serviceNode.is_mapping()) {
                            return ScriptResult::failure(
                                error("AOT Plugin service dependency schema is incomplete"));
                        }
                        const auto* serviceId = field(serviceNode, "id");
                        const auto* serviceVersion = field(serviceNode, "version");
                        if (serviceId == nullptr || !serviceId->is_string()
                            || serviceVersion == nullptr || !serviceVersion->is_integer()) {
                            return ScriptResult::failure(
                                error("AOT Plugin service dependency schema is incomplete"));
                        }
                        const auto serviceVersionValue =
                            serviceVersion->get_value<YamlNode::integer_type>();
                        if (serviceVersionValue <= 0
                            || serviceVersionValue > std::numeric_limits<std::uint32_t>::max()) {
                            return ScriptResult::failure(
                                error("AOT Plugin service dependency version is invalid"));
                        }
                        dependency.services.push_back(
                            AotServiceDependencyManifestV5{stringValue(*serviceId),
                                static_cast<std::uint32_t>(serviceVersionValue)});
                    }
                }
                module.pluginDependencies.push_back(std::move(dependency));
            }
        }
        for (const auto& functionNode :
            functions->get_value_ref<const YamlNode::sequence_type&>()) {
            if (!functionNode.is_mapping())
                return ScriptResult::failure(error("AOT v5 function manifest must be a mapping"));
            const auto* functionId = field(functionNode, "id");
            const auto* functionIdentity = field(functionNode, "identity");
            const auto* signature = field(functionNode, "signature-hash");
            const auto* entry = field(functionNode, "entry");
            const auto* functionEntryAbi = field(functionNode, "entry-abi-version");
            const auto* executionKind = field(functionNode, "execution-kind");
            const auto* osr = field(functionNode, "osr");
            if (functionId == nullptr || !functionId->is_string() || functionIdentity == nullptr
                || !functionIdentity->is_string() || signature == nullptr || !signature->is_string()
                || entry == nullptr || !entry->is_string() || functionEntryAbi == nullptr
                || !functionEntryAbi->is_integer() || executionKind == nullptr
                || !executionKind->is_string() || osr == nullptr || !osr->is_string()
                || stringValue(*executionKind) != "DirectNative") {
                return ScriptResult::failure(error("AOT v5 function is not DirectNative"));
            }
            AotPackageManifestFunctionV5 function;
            function.functionId = stringValue(*functionId);
            function.functionIdentity = stringValue(*functionIdentity);
            function.signatureHash = stringValue(*signature);
            function.entrySymbol = stringValue(*entry);
            const auto entryAbiValue = functionEntryAbi->get_value<YamlNode::integer_type>();
            if (entryAbiValue <= 0 || entryAbiValue > std::numeric_limits<std::uint32_t>::max()) {
                return ScriptResult::failure(error("AOT v5 function ABI is invalid"));
            }
            function.entryAbiVersion = static_cast<std::uint32_t>(entryAbiValue);
            const auto osrValue = stringValue(*osr);
            if (osrValue == "function-boundary") {
                function.osrCapability = AotOsrCapability::FunctionBoundary;
            } else if (osrValue == "function-boundary,operation-boundary") {
                function.osrCapability =
                    AotOsrCapability::FunctionBoundary | AotOsrCapability::OperationBoundary;
            } else if (osrValue == "function-boundary,operation-boundary,resumable") {
                function.osrCapability = AotOsrCapability::FunctionBoundary
                    | AotOsrCapability::OperationBoundary | AotOsrCapability::Resumable;
            } else {
                return ScriptResult::failure(error("AOT v5 function OSR contract is invalid"));
            }
            module.functions.push_back(std::move(function));
        }
        output.modules.push_back(std::move(module));
    }
    return validateAotPackageManifestV5(output, requiredPlatform);
}

} // namespace arp::scriptvm

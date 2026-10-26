#include "editor/project/project_manifest.h"

#include "core/algorithm/dependency_order.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/number_format.h"
#include "resource/resource_manager.h"

#include <algorithm>
#include <exception>
#include <limits>

namespace arp::editor {
namespace {
using Yaml = core::YamlContract;
using Node = Yaml::Node;

bool fail(ProjectDiagnostic& diagnostic, const char* code, std::string_view field)
{
    diagnostic.code = code;
    diagnostic.field = field;
    return false;
}

bool safePath(std::string_view value)
{
    return core::FilePath::isSafeRelativePath(value)
        && std::none_of(value.begin(), value.end(), [](unsigned char c) {
               return c < 32u;
           });
}

bool validText(std::string_view value)
{
    return !value.empty() && std::none_of(value.begin(), value.end(), [](unsigned char c) {
        return c < 32u;
    });
}

core::pmr::String moduleField(std::size_t index, std::string_view suffix)
{
    core::pmr::String field("scriptModules[");
    core::appendUnsigned(field, index);
    field += "].";
    field += suffix;
    return field;
}

Node stringNode(std::string_view value)
{
    return Node(Node::string_type(value.data(), value.size()));
}

bool validScriptAuthoring(const ProjectScriptAuthoring& value, ProjectManifestBudget budget)
{
    const auto representable = [](std::uint64_t bytes) {
        return bytes && bytes <= std::numeric_limits<std::size_t>::max()
            && bytes <= std::uint64_t(INT64_MAX);
    };
    const auto& execution = value.execution;
    if (!representable(value.maxSourceBytes) || !representable(value.preparationBytes)
        || !value.maxInstances || !execution.instructionLimit
        || execution.instructionLimit > std::uint64_t(INT64_MAX) || !execution.recursionLimit
        || !execution.maxFrameValues || !execution.maxContainerValues || !execution.maxCallArguments
        || !execution.maxContinuationFrames || !representable(execution.maxMemoryBytes)
        || value.imports.empty() || value.imports.size() > budget.maxModuleDependencies)
        return false;
    for (std::size_t i = 0; i < value.imports.size(); ++i) {
        if (!validText(value.imports[i]))
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (value.imports[i] == value.imports[j])
                return false;
    }
    return true;
}

ProjectScriptAuthoring readScriptAuthoring(const Node& node, ProjectManifestBudget budget)
{
    Yaml::onlyKeys(node,
        {"contractVersion",
            "maxSourceBytes",
            "preparationBytes",
            "maxInstances",
            "execution",
            "imports"});
    if (Yaml::uint32(node, "contractVersion") != ProjectScriptAuthoring::contractVersion)
        Yaml::fail();
    ProjectScriptAuthoring result;
    result.maxSourceBytes = Yaml::uint64(Yaml::required(node, "maxSourceBytes"));
    result.preparationBytes = Yaml::uint64(Yaml::required(node, "preparationBytes"));
    result.maxInstances = Yaml::uint32(node, "maxInstances");
    const auto& execution = Yaml::required(node, "execution");
    Yaml::onlyKeys(execution,
        {"instructionLimit",
            "recursionLimit",
            "maxFrameValues",
            "maxContainerValues",
            "maxCallArguments",
            "maxContinuationFrames",
            "maxMemoryBytes"});
    auto& values = result.execution;
    values.instructionLimit = Yaml::uint64(Yaml::required(execution, "instructionLimit"));
    values.recursionLimit = Yaml::uint32(execution, "recursionLimit");
    values.maxFrameValues = Yaml::uint32(execution, "maxFrameValues");
    values.maxContainerValues = Yaml::uint32(execution, "maxContainerValues");
    values.maxCallArguments = Yaml::uint32(execution, "maxCallArguments");
    values.maxContinuationFrames = Yaml::uint32(execution, "maxContinuationFrames");
    values.maxMemoryBytes = Yaml::uint64(Yaml::required(execution, "maxMemoryBytes"));
    const auto& imports = Yaml::sequence(node, "imports");
    if (imports.empty() || imports.size() > budget.maxModuleDependencies)
        Yaml::fail();
    for (const auto& import : imports) {
        auto name = Yaml::text(import);
        result.imports.push_back(std::move(name));
    }
    if (!validScriptAuthoring(result, budget))
        Yaml::fail();
    return result;
}

Node writeScriptAuthoring(const ProjectScriptAuthoring& value)
{
    auto node = Node::mapping();
    node["contractVersion"] = ProjectScriptAuthoring::contractVersion;
    node["maxSourceBytes"] = value.maxSourceBytes;
    node["preparationBytes"] = value.preparationBytes;
    node["maxInstances"] = value.maxInstances;
    auto execution = Node::mapping();
    execution["instructionLimit"] = value.execution.instructionLimit;
    execution["recursionLimit"] = value.execution.recursionLimit;
    execution["maxFrameValues"] = value.execution.maxFrameValues;
    execution["maxContainerValues"] = value.execution.maxContainerValues;
    execution["maxCallArguments"] = value.execution.maxCallArguments;
    execution["maxContinuationFrames"] = value.execution.maxContinuationFrames;
    execution["maxMemoryBytes"] = value.execution.maxMemoryBytes;
    node["execution"] = std::move(execution);
    node["imports"] = Node::sequence();
    for (const auto& import : value.imports)
        node["imports"].get_value_ref<Node::sequence_type&>().push_back(stringNode(import));
    return node;
}

// YAML representation belongs to the project contract; runtime world validation
// and creation remain owned by PhysicsWorld2D, not by this codec.
physics2d::WorldConfig readPhysics(const Node& value)
{
    Yaml::onlyKeys(value,
        {"gravity",
            "maxBodies",
            "maxColliders",
            "maxJoints",
            "maxNativeShapes",
            "maxEvents",
            "maxQueryResults",
            "maxChainPoints",
            "substeps",
            "fixedSeconds"});
    const auto& gravity = Yaml::required(value, "gravity");
    Yaml::onlyKeys(gravity, {"x", "y"});
    physics2d::WorldConfig config;
    auto scalar = [](const Node& node, const char* name) {
        const auto number = Yaml::number(node, name);
        if (number > std::numeric_limits<float>::max()
            || number < -std::numeric_limits<float>::max())
            Yaml::fail();
        const auto result = static_cast<float>(number);
        if (!std::isfinite(result) || (number != 0 && result == 0))
            Yaml::fail();
        return result;
    };
    auto capacity = [&](const char* name) {
        const auto result = Yaml::uint32(value, name);
        if (!result)
            Yaml::fail();
        return result;
    };
    config.gravity = {scalar(gravity, "x"), scalar(gravity, "y")};
    config.maxBodies = capacity("maxBodies");
    config.maxColliders = capacity("maxColliders");
    config.maxJoints = capacity("maxJoints");
    config.maxNativeShapes = capacity("maxNativeShapes");
    config.maxEvents = capacity("maxEvents");
    config.maxQueryResults = capacity("maxQueryResults");
    config.maxChainPoints = capacity("maxChainPoints");
    config.substeps = capacity("substeps");
    config.fixedSeconds = scalar(value, "fixedSeconds");
    if (config.fixedSeconds <= 0)
        Yaml::fail();
    return config;
}

Node writePhysics(const physics2d::WorldConfig& config)
{
    auto node = Node::mapping();
    node["gravity"] = Node::mapping();
    node["gravity"]["x"] = config.gravity.x;
    node["gravity"]["y"] = config.gravity.y;
    node["maxBodies"] = config.maxBodies;
    node["maxColliders"] = config.maxColliders;
    node["maxJoints"] = config.maxJoints;
    node["maxNativeShapes"] = config.maxNativeShapes;
    node["maxEvents"] = config.maxEvents;
    node["maxQueryResults"] = config.maxQueryResults;
    node["maxChainPoints"] = config.maxChainPoints;
    node["substeps"] = config.substeps;
    node["fixedSeconds"] = config.fixedSeconds;
    return node;
}

bool validate(const ProjectManifest& manifest,
    ProjectDiagnostic& diagnostic,
    const ProjectManifestBudget& budget)
{
    if (!budget.maxDocumentBytes || !budget.maxAssetPackages || !budget.maxScriptModules
        || !budget.maxModuleDependencies)
        return fail(diagnostic, "PROJECT-BUDGET-INVALID", "budget");
    if (!resource::isResourceUuid(manifest.id))
        return fail(diagnostic, "PROJECT-ID-INVALID", "id");
    if (!validText(manifest.name))
        return fail(diagnostic, "PROJECT-NAME-INVALID", "name");
    if (!validText(manifest.engineContract))
        return fail(diagnostic, "PROJECT-ENGINE-CONTRACT-INVALID", "engineContract");
    if (!manifest.entryScene.empty() && !resource::isResourceUuid(manifest.entryScene))
        return fail(diagnostic, "PROJECT-ENTRY-SCENE-INVALID", "entryScene");
    if (!manifest.editorMetadata.empty() && !safePath(manifest.editorMetadata))
        return fail(diagnostic, "PROJECT-PATH-INVALID", "editorMetadata");
    if (manifest.physics2d) {
        try {
            (void)readPhysics(writePhysics(*manifest.physics2d));
        } catch (const core::YamlContractError&) {
            return fail(diagnostic, "PROJECT-PHYSICS2D-FIELD-INVALID", "physics2d");
        }
    }
    if (manifest.assetPackages.size() > budget.maxAssetPackages
        || manifest.scriptModules.size() > budget.maxScriptModules)
        return fail(diagnostic, "PROJECT-CAPACITY-EXCEEDED", "assetPackages/scriptModules");
    core::pmr::UnorderedSet<core::pmr::String> paths;
    for (const auto& path : manifest.assetPackages) {
        if (!safePath(path))
            return fail(diagnostic, "PROJECT-PATH-INVALID", "assetPackages");
        if (!paths.insert(path).second)
            return fail(diagnostic, "PROJECT-PATH-DUPLICATE", "assetPackages");
    }
    core::pmr::UnorderedSet<core::pmr::String> ids, resources, sources;
    for (std::size_t i = 0; i < manifest.scriptModules.size(); ++i) {
        const auto& module = manifest.scriptModules[i];
        if (!validText(module.id) || !ids.insert(module.id).second)
            return fail(diagnostic, "PROJECT-MODULE-ID-INVALID", moduleField(i, "id"));
        if (!resource::isResourceUuid(module.resourceId)
            || !resources.insert(module.resourceId).second)
            return fail(
                diagnostic, "PROJECT-MODULE-RESOURCE-INVALID", moduleField(i, "resourceId"));
        if (!safePath(module.source) || !sources.insert(module.source).second)
            return fail(diagnostic, "PROJECT-MODULE-SOURCE-INVALID", moduleField(i, "source"));
        if (!module.schemaVersion)
            return fail(
                diagnostic, "PROJECT-MODULE-SCHEMA-INVALID", moduleField(i, "schemaVersion"));
        if (module.dependencies.size() > budget.maxModuleDependencies)
            return fail(diagnostic, "PROJECT-CAPACITY-EXCEEDED", moduleField(i, "dependencies"));
        if (module.authoring && !validScriptAuthoring(*module.authoring, budget))
            return fail(
                diagnostic, "PROJECT-MODULE-AUTHORING-INVALID", moduleField(i, "authoring"));
    }
    core::pmr::Vector<std::size_t> order;
    return ProjectManifestCodec::moduleOrder(manifest, order, diagnostic);
}
} // namespace

bool ProjectManifestCodec::moduleOrder(const ProjectManifest& manifest,
    core::pmr::Vector<std::size_t>& destination,
    ProjectDiagnostic& diagnostic)
{
    core::DependencyOrder::Dependencies dependencies;
    for (std::size_t i = 0; i < manifest.scriptModules.size(); ++i) {
        const auto& module = manifest.scriptModules[i];
        core::pmr::Vector<std::size_t> edges;
        for (const auto& dependency : module.dependencies) {
            const auto found = std::find_if(manifest.scriptModules.begin(),
                manifest.scriptModules.end(),
                [&](const auto& candidate) {
                    return candidate.id == dependency;
                });
            if (found == manifest.scriptModules.end())
                return fail(diagnostic,
                    "PROJECT-MODULE-DEPENDENCY-MISSING",
                    moduleField(i, "dependencies"));
            const auto index = static_cast<std::size_t>(found - manifest.scriptModules.begin());
            if (std::find(edges.begin(), edges.end(), index) != edges.end())
                return fail(diagnostic,
                    "PROJECT-MODULE-DEPENDENCY-DUPLICATE",
                    moduleField(i, "dependencies"));
            edges.push_back(index);
        }
        dependencies.push_back(std::move(edges));
    }
    if (!core::DependencyOrder::resolve(dependencies, destination))
        return fail(diagnostic, "PROJECT-MODULE-DEPENDENCY-CYCLE", "scriptModules");
    diagnostic = {};
    return true;
}

bool ProjectManifestCodec::decode(std::string_view yaml,
    ProjectManifest& destination,
    ProjectDiagnostic& diagnostic,
    ProjectManifestBudget budget)
{
    if (!budget.maxDocumentBytes || !budget.maxAssetPackages || !budget.maxScriptModules
        || !budget.maxModuleDependencies)
        return fail(diagnostic, "PROJECT-BUDGET-INVALID", "budget");
    if (yaml.size() > budget.maxDocumentBytes)
        return fail(diagnostic, "PROJECT-DOCUMENT-BUDGET", "document");
    core::pmr::String field("document");
    try {
        const auto root = Yaml::parseSingleDocument(yaml);
        if (root.is_mapping() && root.contains("format"))
            return fail(diagnostic, "PROJECT-LEGACY-MIGRATION-REQUIRED", "format");
        Yaml::onlyKeys(root,
            {"type",
                "contractVersion",
                "id",
                "name",
                "engineContract",
                "entryScene",
                "execution",
                "assetPackages",
                "scriptModules",
                "physics2d",
                "editorMetadata"});
        field = "type/contractVersion";
        if (Yaml::text(root, "type") != "arp.project"
            || Yaml::uint32(root, "contractVersion") != contractVersion)
            return fail(diagnostic, "PROJECT-VERSION-UNSUPPORTED", field);
        ProjectManifest candidate;
        field = "id";
        candidate.id = Yaml::text(root, "id");
        field = "name";
        candidate.name = Yaml::text(root, "name");
        field = "engineContract";
        candidate.engineContract = Yaml::text(root, "engineContract");
        field = "entryScene";
        const auto& entry = Yaml::required(root, "entryScene");
        if (!entry.is_null()) {
            candidate.entryScene = Yaml::text(entry);
            if (candidate.entryScene.empty())
                return fail(diagnostic, "PROJECT-ENTRY-SCENE-INVALID", field);
        }
        field = "editorMetadata";
        const auto& metadata = Yaml::required(root, "editorMetadata");
        if (!metadata.is_null()) {
            candidate.editorMetadata = Yaml::text(metadata);
            if (candidate.editorMetadata.empty())
                return fail(diagnostic, "PROJECT-PATH-INVALID", field);
        }
        field = "physics2d";
        const auto& physics = Yaml::required(root, "physics2d");
        if (!physics.is_null()) {
            try {
                candidate.physics2d = readPhysics(physics);
            } catch (const core::YamlContractError&) {
                return fail(diagnostic, "PROJECT-PHYSICS2D-FIELD-INVALID", field);
            }
        }
        field = "execution";
        if (Yaml::text(root, "execution") != "Interpreter")
            return fail(diagnostic, "PROJECT-EXECUTION-UNSUPPORTED", field);
        field = "assetPackages";
        const auto& packages = Yaml::sequence(root, "assetPackages");
        if (packages.size() > budget.maxAssetPackages)
            return fail(diagnostic, "PROJECT-CAPACITY-EXCEEDED", field);
        for (const auto& value : packages)
            candidate.assetPackages.push_back(Yaml::text(value));
        field = "scriptModules";
        const auto& modules = Yaml::sequence(root, "scriptModules");
        if (modules.size() > budget.maxScriptModules)
            return fail(diagnostic, "PROJECT-CAPACITY-EXCEEDED", field);
        for (std::size_t i = 0; i < modules.size(); ++i) {
            const auto& value = modules[i];
            field = moduleField(i, "schema");
            Yaml::onlyKeys(value,
                {"id", "resourceId", "source", "schemaVersion", "dependencies", "authoring"});
            ProjectScriptModule module;
            field = moduleField(i, "id");
            module.id = Yaml::text(value, "id");
            field = moduleField(i, "resourceId");
            module.resourceId = Yaml::text(value, "resourceId");
            field = moduleField(i, "source");
            module.source = Yaml::text(value, "source");
            field = moduleField(i, "schemaVersion");
            module.schemaVersion = Yaml::uint32(value, "schemaVersion");
            field = moduleField(i, "dependencies");
            const auto& dependencies = Yaml::sequence(value, "dependencies");
            if (dependencies.size() > budget.maxModuleDependencies)
                return fail(diagnostic, "PROJECT-CAPACITY-EXCEEDED", field);
            for (const auto& dependency : dependencies)
                module.dependencies.push_back(Yaml::text(dependency));
            if (value.contains("authoring")) {
                field = moduleField(i, "authoring");
                try {
                    module.authoring =
                        readScriptAuthoring(Yaml::required(value, "authoring"), budget);
                } catch (const core::YamlContractError&) {
                    return fail(diagnostic, "PROJECT-MODULE-AUTHORING-INVALID", field);
                }
            }
            candidate.scriptModules.push_back(std::move(module));
        }
        if (!validate(candidate, diagnostic, budget))
            return false;
        destination = std::move(candidate);
        diagnostic = {};
        return true;
    } catch (const core::YamlContractError&) {
        return fail(diagnostic, "PROJECT-FIELD-INVALID", field);
    } catch (const std::exception&) {
        return fail(diagnostic, "PROJECT-YAML-INVALID", field);
    }
}

bool ProjectManifestCodec::encode(const ProjectManifest& manifest,
    core::pmr::String& destination,
    ProjectDiagnostic& diagnostic,
    ProjectManifestBudget budget)
{
    if (!validate(manifest, diagnostic, budget))
        return false;
    try {
        auto root = Node::mapping();
        root["type"] = "arp.project";
        root["contractVersion"] = contractVersion;
        root["id"] = stringNode(manifest.id);
        root["name"] = stringNode(manifest.name);
        root["engineContract"] = stringNode(manifest.engineContract);
        root["entryScene"] = manifest.entryScene.empty() ? Node() : stringNode(manifest.entryScene);
        root["editorMetadata"] =
            manifest.editorMetadata.empty() ? Node() : stringNode(manifest.editorMetadata);
        root["physics2d"] = manifest.physics2d ? writePhysics(*manifest.physics2d) : Node();
        root["execution"] = "Interpreter";
        root["assetPackages"] = Node::sequence();
        for (const auto& path : manifest.assetPackages)
            root["assetPackages"].get_value_ref<Node::sequence_type&>().push_back(stringNode(path));
        root["scriptModules"] = Node::sequence();
        for (const auto& module : manifest.scriptModules) {
            auto value = Node::mapping();
            value["id"] = stringNode(module.id);
            value["resourceId"] = stringNode(module.resourceId);
            value["source"] = stringNode(module.source);
            value["schemaVersion"] = module.schemaVersion;
            if (module.authoring)
                value["authoring"] = writeScriptAuthoring(*module.authoring);
            value["dependencies"] = Node::sequence();
            for (const auto& dependency : module.dependencies)
                value["dependencies"].get_value_ref<Node::sequence_type&>().push_back(
                    stringNode(dependency));
            root["scriptModules"].get_value_ref<Node::sequence_type&>().push_back(std::move(value));
        }
        auto candidate = Yaml::serialize(root);
        if (candidate.size() > budget.maxDocumentBytes)
            return fail(diagnostic, "PROJECT-DOCUMENT-BUDGET", "document");
        destination = std::move(candidate);
        diagnostic = {};
        return true;
    } catch (const core::YamlContractError&) {
        return fail(diagnostic, "PROJECT-FIELD-INVALID", "document");
    } catch (const std::exception&) {
        return fail(diagnostic, "PROJECT-YAML-INVALID", "document");
    }
}

bool ProjectManifestCodec::validateForLaunch(const ProjectManifest& manifest,
    const ProjectLaunchValidation& context,
    ProjectDiagnostic& diagnostic,
    ProjectManifestBudget budget)
{
    if (!validate(manifest, diagnostic, budget))
        return false;
    if (context.engineContract.empty() || manifest.engineContract != context.engineContract)
        return fail(diagnostic, "PROJECT-ENGINE-CONTRACT-MISMATCH", "engineContract");
    if (manifest.entryScene.empty())
        return fail(diagnostic, "PROJECT-ENTRY-SCENE-REQUIRED", "entryScene");
    if (!context.isReadyResource)
        return fail(diagnostic, "PROJECT-RESOURCE-VALIDATOR-REQUIRED", "entryScene");
    if (!context.isReadyResource(
            context.resourceContext, "SceneAssetResource", manifest.entryScene))
        return fail(diagnostic, "PROJECT-ENTRY-SCENE-NOT-READY", "entryScene");
    for (std::size_t i = 0; i < manifest.scriptModules.size(); ++i) {
        if (!context.isPreparedModule)
            return fail(diagnostic, "PROJECT-MODULE-VALIDATOR-REQUIRED", "scriptModules");
        if (!context.isPreparedModule(context.resourceContext, manifest.scriptModules[i]))
            return fail(diagnostic, "PROJECT-MODULE-NOT-PREPARED", moduleField(i, "id"));
    }
    diagnostic = {};
    return true;
}

bool ProjectManifestCodec::resolvePath(const std::filesystem::path& root,
    std::string_view relative,
    ProjectPathIntent intent,
    std::filesystem::path& destination,
    ProjectDiagnostic& diagnostic)
{
    if (!safePath(relative))
        return fail(diagnostic, "PROJECT-PATH-INVALID", relative);
    std::error_code error;
    const auto canonicalRoot = std::filesystem::canonical(root, error);
    if (error || !std::filesystem::is_directory(canonicalRoot, error) || error)
        return fail(diagnostic, "PROJECT-ROOT-INVALID", "root");
    const auto target = canonicalRoot / std::filesystem::u8path(relative);
    const auto targetStatus = std::filesystem::symlink_status(target, error);
    if (error && error != std::errc::no_such_file_or_directory)
        return fail(diagnostic, "PROJECT-PATH-TYPE-INVALID", relative);
    error.clear();
    if (std::filesystem::is_symlink(targetStatus)
        && (!std::filesystem::exists(target, error) || error))
        return fail(diagnostic, "PROJECT-PATH-TYPE-INVALID", relative);
    auto candidate =
        (intent == ProjectPathIntent::NewFile || intent == ProjectPathIntent::NewDirectory)
        ? std::filesystem::weakly_canonical(target, error)
        : std::filesystem::canonical(target, error);
    if (error || !core::FilePath::contains(canonicalRoot, candidate))
        return fail(diagnostic, "PROJECT-PATH-OUTSIDE-ROOT", relative);
    bool valid = false;
    switch (intent) {
    case ProjectPathIntent::ExistingFile:
        valid = std::filesystem::is_regular_file(candidate, error);
        break;
    case ProjectPathIntent::ExistingDirectory:
        valid = std::filesystem::is_directory(candidate, error);
        break;
    case ProjectPathIntent::NewFile:
        valid = std::filesystem::is_directory(candidate.parent_path(), error);
        if (valid && !error) {
            const bool exists = std::filesystem::exists(candidate, error);
            if (!error && exists)
                valid = std::filesystem::is_regular_file(candidate, error);
        }
        break;
    case ProjectPathIntent::NewDirectory:
        valid = targetStatus.type() == std::filesystem::file_type::not_found
            && std::filesystem::is_directory(candidate.parent_path(), error);
        break;
    }
    if (!valid || error)
        return fail(diagnostic, "PROJECT-PATH-TYPE-INVALID", relative);
    destination = std::move(candidate);
    diagnostic = {};
    return true;
}

} // namespace arp::editor

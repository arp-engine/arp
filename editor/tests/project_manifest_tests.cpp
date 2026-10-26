#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_manifest.h"
#include "editor/project/project_metadata.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::editor;

constexpr std::string_view document = R"(type: arp.project
contractVersion: 1
id: a75b3294-d283-4b12-92d8-7d522b14eae2
name: Test project
engineContract: test-engine/1
entryScene: null
editorMetadata: null
physics2d: null
execution: Interpreter
assetPackages: [Assets/base.arpkg]
scriptModules:
  - id: gameplay
    resourceId: a75b3294-d283-4b12-92d8-7d522b14eae3
    source: scripts/gameplay.arp
    schemaVersion: 1
    dependencies: [foundation]
  - id: foundation
    resourceId: a75b3294-d283-4b12-92d8-7d522b14eae4
    source: scripts/foundation.arp
    schemaVersion: 1
    dependencies: []
)";

ProjectManifest project()
{
    ProjectManifest manifest;
    ProjectDiagnostic diagnostic;
    assert(ProjectManifestCodec::decode(document, manifest, diagnostic));
    assert(!diagnostic.code);
    return manifest;
}

void rejects(std::string_view yaml, std::string_view code, ProjectManifestBudget budget = {})
{
    auto manifest = project();
    ProjectDiagnostic diagnostic;
    assert(!ProjectManifestCodec::decode(yaml, manifest, diagnostic, budget));
    if (!diagnostic.code || code != diagnostic.code)
        std::fprintf(stderr,
            "Expected %.*s; received %s at %s\n",
            int(code.size()),
            code.data(),
            diagnostic.code ? diagnostic.code : "<none>",
            diagnostic.field.c_str());
    assert(diagnostic.code && code == diagnostic.code && !diagnostic.field.empty());
    assert(manifest.name == "Test project" && manifest.scriptModules.size() == 2);
}

void replaceAndReject(std::string_view from, std::string_view to, std::string_view code)
{
    core::pmr::String yaml(document);
    const auto at = yaml.find(from);
    assert(at != core::pmr::String::npos);
    yaml.replace(at, from.size(), to);
    rejects(yaml, code);
}

void contractTests()
{
    auto manifest = project();
    ProjectDiagnostic diagnostic;
    // External YAML writers may sort fields; key order is not a project contract.
    ProjectManifest external;
    constexpr std::string_view reordered = R"(assetPackages: []
contractVersion: 1
editorMetadata: null
engineContract: arp.scene-asset/2
entryScene: 64822b67-bdab-4927-9202-ae27c1c34d60
execution: Interpreter
id: a7e4ee41-6488-4daf-9c49-87a6efc1a5f2
name: CLI identity project
physics2d: null
scriptModules: []
type: arp.project
)";
    const bool accepted = ProjectManifestCodec::decode(reordered, external, diagnostic);
    if (!accepted)
        std::fprintf(stderr, "%s: %s\n", diagnostic.code, diagnostic.field.c_str());
    assert(accepted && external.entryScene == "64822b67-bdab-4927-9202-ae27c1c34d60");
    core::pmr::Vector<std::size_t> order;
    assert(ProjectManifestCodec::moduleOrder(manifest, order, diagnostic));
    assert(order.size() == 2 && order[0] == 1 && order[1] == 0);
    core::pmr::String first, second;
    assert(ProjectManifestCodec::encode(manifest, first, diagnostic));
    ProjectManifest restored;
    assert(ProjectManifestCodec::decode(first, restored, diagnostic));
    assert(ProjectManifestCodec::encode(restored, second, diagnostic));
    assert(first == second && restored.entryScene.empty());

    replaceAndReject("contractVersion: 1", "contractVersion: 2", "PROJECT-VERSION-UNSUPPORTED");
    replaceAndReject("entryScene: null", "entryScene: ''", "PROJECT-ENTRY-SCENE-INVALID");
    replaceAndReject("entryScene: null", "entryScene: scene.yaml", "PROJECT-ENTRY-SCENE-INVALID");
    replaceAndReject("execution: Interpreter", "execution: Aot", "PROJECT-EXECUTION-UNSUPPORTED");
    replaceAndReject("name: Test project", "name: ''", "PROJECT-NAME-INVALID");
    replaceAndReject(
        "name: Test project", "name: Test project\nunknown: 1", "PROJECT-FIELD-INVALID");
    replaceAndReject("Assets/base.arpkg", "../outside", "PROJECT-PATH-INVALID");
    replaceAndReject(
        "Assets/base.arpkg", "Assets/base.arpkg, Assets/base.arpkg", "PROJECT-PATH-DUPLICATE");
    replaceAndReject("dependencies: [foundation]",
        "dependencies: [missing]",
        "PROJECT-MODULE-DEPENDENCY-MISSING");
    replaceAndReject("dependencies: [foundation]",
        "dependencies: [foundation, foundation]",
        "PROJECT-MODULE-DEPENDENCY-DUPLICATE");
    replaceAndReject(
        "dependencies: []", "dependencies: [gameplay]", "PROJECT-MODULE-DEPENDENCY-CYCLE");
    replaceAndReject("id: foundation", "id: gameplay", "PROJECT-MODULE-ID-INVALID");
    replaceAndReject("schemaVersion: 1", "schemaVersion: 0", "PROJECT-MODULE-SCHEMA-INVALID");
    rejects("format: ArpEditorProject\nversion: 1\nassetPackages: []\n",
        "PROJECT-LEGACY-MIGRATION-REQUIRED");
    rejects("---\n{}\n---\n{}\n", "PROJECT-FIELD-INVALID");
    auto budget = ProjectManifestBudget{};
    budget.maxScriptModules = 1;
    rejects(document, "PROJECT-CAPACITY-EXCEEDED", budget);
    budget = {};
    budget.maxDocumentBytes = 8;
    rejects(document, "PROJECT-DOCUMENT-BUDGET", budget);
    budget = {};
    budget.maxModuleDependencies = 0;
    rejects(document, "PROJECT-BUDGET-INVALID", budget);

    manifest.scriptModules[1].dependencies.push_back("gameplay");
    const auto previousOrder = order;
    assert(!ProjectManifestCodec::moduleOrder(manifest, order, diagnostic));
    assert(order == previousOrder);
    second = "old output";
    assert(!ProjectManifestCodec::encode(manifest, second, diagnostic));
    assert(second == "old output");
}

struct Readiness {
    bool ready = false;
    bool prepared = false;
};

void launchTests()
{
    auto manifest = project();
    ProjectDiagnostic diagnostic;
    Readiness readiness;
    ProjectLaunchValidation context{"test-engine/1", &readiness};
    assert(!ProjectManifestCodec::validateForLaunch(manifest, context, diagnostic));
    assert(std::string_view(diagnostic.code) == "PROJECT-ENTRY-SCENE-REQUIRED");

    manifest.entryScene = "a75b3294-d283-4b12-92d8-7d522b14eae5";
    assert(!ProjectManifestCodec::validateForLaunch(manifest, context, diagnostic));
    assert(std::string_view(diagnostic.code) == "PROJECT-RESOURCE-VALIDATOR-REQUIRED");
    context.isReadyResource = [](void* state, std::string_view type, std::string_view id) {
        assert(type == "SceneAssetResource" && id == "a75b3294-d283-4b12-92d8-7d522b14eae5");
        return static_cast<Readiness*>(state)->ready;
    };
    assert(!ProjectManifestCodec::validateForLaunch(manifest, context, diagnostic));
    assert(std::string_view(diagnostic.code) == "PROJECT-ENTRY-SCENE-NOT-READY");
    readiness.ready = true;
    assert(!ProjectManifestCodec::validateForLaunch(manifest, context, diagnostic));
    assert(std::string_view(diagnostic.code) == "PROJECT-MODULE-VALIDATOR-REQUIRED");
    context.isPreparedModule = [](void* state, const ProjectScriptModule&) {
        return static_cast<Readiness*>(state)->prepared;
    };
    assert(!ProjectManifestCodec::validateForLaunch(manifest, context, diagnostic));
    assert(std::string_view(diagnostic.code) == "PROJECT-MODULE-NOT-PREPARED");
    readiness.prepared = true;
    assert(ProjectManifestCodec::validateForLaunch(manifest, context, diagnostic));
    context.engineContract = {};
    assert(!ProjectManifestCodec::validateForLaunch(manifest, context, diagnostic));
    assert(std::string_view(diagnostic.code) == "PROJECT-ENGINE-CONTRACT-MISMATCH");
}

void pathTests()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("arp-project-contract-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "project" / "Assets");
    std::ofstream(root / "outside.txt") << "outside";
    std::ofstream(root / "project" / "Assets" / "data.yaml") << "inside";
    ProjectDiagnostic diagnostic;
    std::filesystem::path destination;
    const auto projectRoot = root / "project";
    assert(ProjectManifestCodec::resolvePath(
        projectRoot, "Assets/data.yaml", ProjectPathIntent::ExistingFile, destination, diagnostic));
    const auto old = destination;
    assert(!ProjectManifestCodec::resolvePath(
        projectRoot, "../outside.txt", ProjectPathIntent::ExistingFile, destination, diagnostic));
    assert(destination == old);
    assert(!ProjectManifestCodec::resolvePath(projectRoot,
        std::string_view("Assets/a\0b", 10),
        ProjectPathIntent::NewFile,
        destination,
        diagnostic));
    assert(ProjectManifestCodec::resolvePath(
        projectRoot, "Assets/new.yaml", ProjectPathIntent::NewFile, destination, diagnostic));
    assert(ProjectManifestCodec::resolvePath(projectRoot,
        "Assets/new-package",
        ProjectPathIntent::NewDirectory,
        destination,
        diagnostic));
    const auto newDirectory = destination;
    for (const auto path :
        {"Assets", "Assets/data.yaml", "missing/new", "../outside", "Assets/../Assets"}) {
        assert(!ProjectManifestCodec::resolvePath(
            projectRoot, path, ProjectPathIntent::NewDirectory, destination, diagnostic));
        assert(destination == newDirectory);
    }
    assert(!ProjectManifestCodec::resolvePath(
        projectRoot, "Assets", ProjectPathIntent::ExistingFile, destination, diagnostic));
    std::error_code error;
    std::filesystem::create_symlink(root / "outside.txt", projectRoot / "link.yaml", error);
    if (!error) {
        assert(!ProjectManifestCodec::resolvePath(
            projectRoot, "link.yaml", ProjectPathIntent::ExistingFile, destination, diagnostic));
        assert(!ProjectManifestCodec::resolvePath(
            projectRoot, "link.yaml", ProjectPathIntent::NewFile, destination, diagnostic));
    }
    std::filesystem::remove_all(root);
}

void fixtureSmoke()
{
    const auto source = core::readTextFile(
        std::filesystem::path(ARP_SOURCE_DIR) / "editor/tests/fixtures/project/authoring.arp.yaml",
        ProjectManifestBudget{}.maxDocumentBytes);
    assert(source.success);
    ProjectManifest manifest;
    ProjectDiagnostic diagnostic;
    assert(ProjectManifestCodec::decode(source.text, manifest, diagnostic));
    assert(manifest.assetPackages.empty() && manifest.scriptModules.empty());
    assert(!ProjectManifestCodec::validateForLaunch(
        manifest, {"arp-project-contract-fixture/1"}, diagnostic));
    assert(std::string_view(diagnostic.code) == "PROJECT-ENTRY-SCENE-REQUIRED");

    const auto viewportSource = core::readTextFile(std::filesystem::path(ARP_SOURCE_DIR)
            / "tests/fixtures/editor_tilemap_project/project.arp.yaml",
        ProjectManifestBudget{}.maxDocumentBytes);
    assert(viewportSource.success);
    assert(ProjectManifestCodec::decode(viewportSource.text, manifest, diagnostic));
    assert(manifest.id == "e2a35978-e185-41c6-aadf-9729d9228344");
    assert(manifest.physics2d && manifest.physics2d->maxBodies == 32u);
}

void metadataTests()
{
    constexpr std::string_view valid = "type: arp.editor-project\ncontractVersion: 1\n"
                                       "terrainPreviews: [terrain/preview.yaml]\n";
    ProjectMetadata metadata;
    ProjectDiagnostic diagnostic;
    assert(ProjectMetadataCodec::decode(valid, metadata, diagnostic));
    assert(metadata.terrainPreviews.size() == 1 && !diagnostic.code);
    const auto original = metadata.terrainPreviews;
    const auto reject =
        [&](std::string_view yaml, std::string_view code, ProjectManifestBudget budget = {}) {
            assert(!ProjectMetadataCodec::decode(yaml, metadata, diagnostic, budget));
            assert(diagnostic.code && code == diagnostic.code);
            assert(metadata.terrainPreviews == original);
        };
    reject("type: arp.editor-project\ncontractVersion: 2\nterrainPreviews: []\n",
        "PROJECT-METADATA-VERSION-UNSUPPORTED");
    reject("type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: [../outside]\n",
        "PROJECT-METADATA-PATH-INVALID");
    reject("type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: [a, a]\n",
        "PROJECT-METADATA-PATH-DUPLICATE");
    reject("type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: [1]\n",
        "PROJECT-METADATA-FIELD-INVALID");
    reject("type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: []\nunknown: true\n",
        "PROJECT-METADATA-FIELD-INVALID");
    reject("type: arp.editor-project\ncontractVersion: 1\n", "PROJECT-METADATA-FIELD-INVALID");
    reject("type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: []\n---\n",
        "PROJECT-METADATA-FIELD-INVALID");
    auto budget = ProjectManifestBudget{};
    budget.maxDocumentBytes = 1;
    reject(valid, "PROJECT-METADATA-DOCUMENT-BUDGET", budget);
    budget = {};
    budget.maxAssetPackages = 1;
    reject("type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: [a, b]\n",
        "PROJECT-METADATA-CAPACITY-EXCEEDED",
        budget);
}

void scriptAuthoringTests()
{
    using Y = core::YamlContract;
    auto manifest = project();
    assert(!manifest.scriptModules[0].authoring && !manifest.scriptModules[1].authoring);
    ProjectScriptAuthoring config;
    config.maxSourceBytes = 65536;
    config.preparationBytes = 16 * 1024 * 1024;
    config.maxInstances = 32;
    config.execution = {100000, 64, 1024, 1024, 16, 64, 16 * 1024 * 1024};
    config.imports = {"arp_scene"};
    manifest.scriptModules[0].authoring = config;
    core::pmr::String yaml, again;
    ProjectDiagnostic diagnostic;
    assert(ProjectManifestCodec::encode(manifest, yaml, diagnostic));
    ProjectManifest restored;
    assert(ProjectManifestCodec::decode(yaml, restored, diagnostic));
    assert(restored.scriptModules[0].authoring && !restored.scriptModules[1].authoring);
    const auto& value = *restored.scriptModules[0].authoring;
    assert(value.maxSourceBytes == config.maxSourceBytes
        && value.preparationBytes == config.preparationBytes
        && value.maxInstances == config.maxInstances && value.imports == config.imports
        && value.execution.maxMemoryBytes == config.execution.maxMemoryBytes);
    assert(ProjectManifestCodec::encode(restored, again, diagnostic) && yaml == again);
    const auto node = Y::parseSingleDocument(yaml);
    for (const char* key : {"contractVersion",
             "maxSourceBytes",
             "preparationBytes",
             "maxInstances",
             "execution",
             "imports"}) {
        auto malformed = node;
        auto& authoring = malformed["scriptModules"][0]["authoring"];
        if (std::string_view(key) == "contractVersion")
            authoring[key] = 2;
        else if (std::string_view(key) == "execution")
            authoring[key] = Y::Node::mapping();
        else if (std::string_view(key) == "imports")
            authoring[key] = Y::Node::sequence();
        else
            authoring[key] = 0;
        rejects(Y::serialize(malformed), "PROJECT-MODULE-AUTHORING-INVALID");
    }
    for (const char* key : {"instructionLimit",
             "recursionLimit",
             "maxFrameValues",
             "maxContainerValues",
             "maxCallArguments",
             "maxContinuationFrames",
             "maxMemoryBytes"}) {
        auto malformed = node;
        malformed["scriptModules"][0]["authoring"]["execution"][key] = 0;
        rejects(Y::serialize(malformed), "PROJECT-MODULE-AUTHORING-INVALID");
    }
    auto malformed = node;
    malformed["scriptModules"][0]["authoring"]["unknown"] = true;
    rejects(Y::serialize(malformed), "PROJECT-MODULE-AUTHORING-INVALID");
    malformed = node;
    malformed["scriptModules"][0]["authoring"]["imports"]
        .get_value_ref<Y::Node::sequence_type&>()
        .push_back("arp_scene");
    rejects(Y::serialize(malformed), "PROJECT-MODULE-AUTHORING-INVALID");
    const auto before = again;
    manifest.scriptModules[0].authoring->maxSourceBytes = UINT64_MAX;
    assert(!ProjectManifestCodec::encode(manifest, again, diagnostic) && before == again);
    assert(std::string_view(diagnostic.code) == "PROJECT-MODULE-AUTHORING-INVALID");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--fixture") {
        fixtureSmoke();
        return 0;
    }
    contractTests();
    launchTests();
    pathTests();
    metadataTests();
    scriptAuthoringTests();
}

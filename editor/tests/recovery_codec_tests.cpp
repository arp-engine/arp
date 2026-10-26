#include "core/encoding/base64.h"
#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/atomic_file_service.h"
#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_manifest.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_recovery_codec.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/script_document_session.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
using namespace arp;
using namespace arp::editor;
using Yaml = core::YamlContract;
using Node = Yaml::Node;

using tests::RecoveryFixture;

void roundTrip(const RecoveryFixture& fixture)
{
    auto value = fixture.snapshot();
    const auto yaml = RecoveryFixture::encode(value);
    assert(yaml.find("arp.editor-recovery") != core::pmr::String::npos);
    EditorRecoverySnapshot decoded;
    core::pmr::String diagnostic;
    assert(EditorRecoveryCodec::decode(yaml, decoded, diagnostic));
    assert(decoded.sessionId == value.sessionId && decoded.sequence == 1
        && decoded.workspaceEpoch == 7 && decoded.projectRoot == value.projectRoot
        && decoded.activeScript == value.activeScript && decoded.workspace == "Script"
        && !decoded.project && decoded.scene.yaml == value.scene.yaml
        && decoded.scene.savedFileBytes == value.scene.savedFileBytes
        && decoded.scene.savedContentHash == value.scene.savedContentHash
        && decoded.scripts.size() == 1);
    const auto& script = decoded.scripts.front();
    assert(script.text == value.scripts.front().text
        && script.savedText == value.scripts.front().savedText
        && script.savedFileBytes == value.scripts.front().savedFileBytes
        && script.savedFileBytes.find('\0') != core::pmr::String::npos && script.newline == "\r\n"
        && script.cursor == 4 && script.anchor == 1 && script.scrollLine == 2);
    assert(RecoveryFixture::encode(decoded) == yaml);
    // Baseline bytes retain their spelling; semantic save-point verification
    // requires the later resource-ready candidate, not a wire-byte hash.
    core::pmr::String crlf;
    for (const auto c : value.scene.savedFileBytes) {
        if (c == '\n')
            crlf += '\r';
        crlf += c;
    }
    value.scene.savedFileBytes = crlf;
    assert(EditorRecoveryCodec::validate(value, diagnostic));

    value.project.emplace();
    value.project->id = value.sessionId;
    value.project->savedFileBytes =
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n");
    auto manifest = Yaml::parseSingleDocument(value.project->savedFileBytes);
    manifest["name"] = "Unsaved project name";
    value.project->draftYaml = Yaml::serialize(manifest);
    const auto projectYaml = RecoveryFixture::encode(value);
    assert(EditorRecoveryCodec::decode(projectYaml, decoded, diagnostic));
    assert(decoded.project && decoded.project->savedFileBytes == value.project->savedFileBytes
        && decoded.project->draftYaml != decoded.project->savedFileBytes);
    manifest["assetPackages"].get_value_ref<Node::sequence_type&>().push_back(Node("assets/main"));
    value.project->savedFileBytes = Yaml::serialize(manifest);
    value.project->draftYaml = value.project->savedFileBytes;
    value.project->packages.push_back(
        {"assets/main", core::Sha256::hexDigest("explicit fixture binding bytes")});
    assert(EditorRecoveryCodec::decode(RecoveryFixture::encode(value), decoded, diagnostic)
        && decoded.project->packages[0].directory == "assets/main");
    auto invalidProject = value;
    invalidProject.project->packages.clear();
    assert(!EditorRecoveryCodec::validate(invalidProject, diagnostic));
    invalidProject = value;
    invalidProject.project->id = "ffffffff-ffff-4fff-8fff-ffffffffffff";
    assert(!EditorRecoveryCodec::validate(invalidProject, diagnostic));
    invalidProject = value;
    invalidProject.project->packages[0].directory = "../escape";
    assert(!EditorRecoveryCodec::validate(invalidProject, diagnostic));
    value.scene.savedContentHash.clear();
    value.scene.path.clear();
    value.scene.boundTarget.clear();
    value.scene.savedFileBytes.clear();
    value.scripts.clear();
    value.activeScript.reset();
    value.workspace = "3D";
    assert(EditorRecoveryCodec::decode(RecoveryFixture::encode(value), decoded, diagnostic)
        && decoded.scene.path.empty() && decoded.scene.savedFileBytes.empty()
        && decoded.scripts.empty() && !decoded.activeScript);
    std::puts(
        "PASS recovery raw bytes/CRLF/NUL, save-point hash, independent project draft, unbound scene and deterministic round-trip");
}

void invalidRecords(const RecoveryFixture& fixture)
{
    const auto value = fixture.snapshot();
    const auto yaml = RecoveryFixture::encode(value);
    const auto source = Yaml::parseSingleDocument(yaml);
    auto reject = [&](const Node& changed) {
        auto destination = value;
        destination.sequence = 42;
        core::pmr::String diagnostic;
        assert(!EditorRecoveryCodec::decode(Yaml::serialize(changed), destination, diagnostic));
        assert(!diagnostic.empty() && destination.sequence == 42
            && destination.scene.yaml == value.scene.yaml
            && destination.scripts.front().savedFileBytes == value.scripts.front().savedFileBytes);
    };
    auto changed = source;
    changed["contractVersion"] = 99;
    reject(changed);
    changed = source;
    changed["projectRoot"] = "../project";
    reject(changed);
    changed = source;
    changed["sessionId"] = "not-an-identity";
    reject(changed);
    changed = source;
    changed["sequence"] = 0;
    reject(changed);
    changed = source;
    changed["workspace"] = "Unknown";
    reject(changed);
    changed = source;
    changed["activeScript"] = 1;
    reject(changed);
    changed = source;
    changed["extra"] = true;
    reject(changed);
    changed = source;
    changed["scene"]["path"] = "";
    reject(changed);
    changed = source;
    changed["scene"]["boundTarget"] = "../escape";
    reject(changed);
    changed = source;
    changed["scene"]["disk"] = Node();
    reject(changed);
    changed = source;
    changed["scene"]["savedContentHash"] = "not-a-content-hash";
    reject(changed);
    changed = source;
    changed["scripts"][0]["text"]["encoding"] = "utf8";
    reject(changed);
    changed = source;
    changed["scripts"][0]["text"]["bytes"] = 0;
    reject(changed);
    changed = source;
    changed["scripts"][0]["text"]["data"] = "!!!!";
    reject(changed);
    changed = source;
    // Both AB== and AA== decode to zero in a permissive decoder. Only the
    // canonical representation is admitted by the recovery contract.
    changed["scripts"][0]["text"]["data"] = "AB==";
    changed["scripts"][0]["text"]["bytes"] = 1;
    changed["scripts"][0]["cursor"] = 0;
    changed["scripts"][0]["anchor"] = 0;
    reject(changed);
    changed["scripts"][0]["text"]["data"] = "AA==";
    EditorRecoverySnapshot canonical;
    core::pmr::String canonicalDiagnostic;
    assert(EditorRecoveryCodec::decode(Yaml::serialize(changed), canonical, canonicalDiagnostic)
        && canonical.scripts[0].text.size() == 1u && canonical.scripts[0].text[0] == '\0');
    changed = source;
    changed["scripts"][0]["cursor"] = 100000;
    reject(changed);
    changed = source;
    changed["scripts"][0]["path"] = "wrong.txt";
    reject(changed);
    changed = source;
    changed["scripts"].get_value_ref<Node::sequence_type&>().push_back(changed["scripts"][0]);
    reject(changed);
    core::pmr::String diagnostic;
    EditorRecoverySnapshot destination;
    assert(!EditorRecoveryCodec::decode("type: [", destination, diagnostic));
    assert(!EditorRecoveryCodec::decode(yaml + "---\ntype: extra\n", destination, diagnostic));
    auto missing = source;
    missing.get_value_ref<Node::mapping_type&>().erase(Node("workspaceEpoch"));
    reject(missing);
    auto encodeReject = [&](auto modify) {
        auto typed = value;
        modify(typed);
        core::pmr::String untouched("unchanged");
        assert(
            !EditorRecoveryCodec::encode(typed, untouched, diagnostic) && untouched == "unchanged");
    };
    encodeReject([](auto& typed) {
        typed.scripts[0].savedText = "different";
    });
    encodeReject([](auto& typed) {
        typed.scene.boundTarget.clear();
    });
    encodeReject([](auto& typed) {
        typed.projectRoot += "/../escape";
    });
    encodeReject([](auto& typed) {
        typed.scripts[0].newline = "\r";
    });
    encodeReject([](auto& typed) {
        typed.scripts[0].path = core::pmr::String("\xff.arp", 5u);
    });
    std::puts(
        "PASS strict fields/version/identity/paths, canonical base64, save-point and script consistency, failed destination preservation");
}

void budgetContract(const RecoveryFixture& fixture)
{
    const auto value = fixture.snapshot();
    const auto yaml = RecoveryFixture::encode(value);
    EditorRecoveryBudget budget;
    core::pmr::String diagnostic, encoded("unchanged");
    EditorRecoverySnapshot decoded;
    budget.maxEnvelopeBytes = yaml.size();
    assert(EditorRecoveryCodec::decode(yaml, decoded, diagnostic, budget));
    --budget.maxEnvelopeBytes;
    assert(!EditorRecoveryCodec::decode(yaml, decoded, diagnostic, budget)
        && diagnostic == "EDITOR-RECOVERY-ENVELOPE-LIMIT");
    assert(
        !EditorRecoveryCodec::encode(value, encoded, diagnostic, budget) && encoded == "unchanged");
    budget = {};
    budget.maxPayloadBytes = 1;
    assert(!EditorRecoveryCodec::decode(yaml, decoded, diagnostic, budget));
    assert(!EditorRecoveryCodec::encode(value, encoded, diagnostic, budget));
    budget = {};
    budget.maxPathBytes = 4;
    assert(!EditorRecoveryCodec::decode(yaml, decoded, diagnostic, budget));
    budget = {};
    budget.maxRegionBytes = 1;
    assert(!EditorRecoveryCodec::validateBudget(budget, diagnostic));
    budget = {};
    ++budget.maxScripts;
    assert(!EditorRecoveryCodec::validateBudget(budget, diagnostic));
    budget = {};
    budget.slotsPerSession = 1;
    assert(!EditorRecoveryCodec::validateBudget(budget, diagnostic));
    budget = {};
    budget.snapshotIntervalSeconds = 0;
    assert(!EditorRecoveryCodec::validateBudget(budget, diagnostic));
    budget = {};
    budget.maxRegionPublishAttempts = 0;
    assert(!EditorRecoveryCodec::validateBudget(budget, diagnostic));
    budget = {};
    ++budget.maxRegionPublishAttempts;
    assert(!EditorRecoveryCodec::validateBudget(budget, diagnostic));
    budget = {};
    ++budget.regionPublishRetryMilliseconds;
    assert(!EditorRecoveryCodec::validateBudget(budget, diagnostic));
    budget = {};
    budget.maxRegionPublishAttempts = 1;
    budget.regionPublishRetryMilliseconds = 0;
    assert(EditorRecoveryCodec::validateBudget(budget, diagnostic));
    auto many = value;
    while (many.scripts.size() < EditorRecoveryBudget{}.maxScripts) {
        auto script = value.scripts.front();
        script.path = "rules-" + core::pmr::String(std::to_string(many.scripts.size())) + ".arp";
        many.scripts.push_back(std::move(script));
    }
    assert(EditorRecoveryCodec::decode(RecoveryFixture::encode(many), decoded, diagnostic));
    many.scripts.push_back(value.scripts.front());
    assert(!EditorRecoveryCodec::encode(many, encoded, diagnostic));
    budget = {};
    // Each field fits this reduced quota; their combined live/saved/baseline
    // copies exceed it and must still be rejected.
    budget.maxPayloadBytes = value.scene.yaml.size() + 128u;
    assert(!EditorRecoveryCodec::encode(value, encoded, diagnostic, budget));
    auto large = value;
    large.scripts[0].text.assign(ScriptDocumentSession::maxScriptBytes, 'x');
    assert(EditorRecoveryCodec::validate(large, diagnostic));
    large.scripts[0].text += 'x';
    assert(!EditorRecoveryCodec::validate(large, diagnostic));
    large = value;
    large.scene.yaml.assign(EditorSceneAuthoringCodec::maxDocumentBytes + 1u, ' ');
    assert(!EditorRecoveryCodec::validate(large, diagnostic));
    large = value;
    large.project.emplace();
    large.project->draftYaml.assign(ProjectManifestBudget{}.maxDocumentBytes + 1u, ' ');
    assert(!EditorRecoveryCodec::validate(large, diagnostic));
    std::puts(
        "PASS exact envelope boundary, aggregate payload/path limits and explicit session/slot/disk/interval budget rejection");
}

void externalInputFields(const RecoveryFixture& fixture)
{
    auto value = fixture.snapshot();
    value.project.emplace();
    auto manifest = Yaml::parseSingleDocument(
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n", "editor.yaml"));
    value.project->id = Yaml::text(manifest, "id");
    value.project->savedFileBytes = value.project->draftYaml = Yaml::serialize(manifest);
    const core::pmr::String metadata(
        "type: arp.editor-project\ncontractVersion: 1\nterrainPreviews: []\n");
    value.project->metadata = EditorRecoveryMetadata{"editor.yaml", metadata};
    value.project->inputFiles.push_back(
        {"editor.yaml", metadata.size(), core::Sha256::hexDigest(metadata)});
    const auto yaml = RecoveryFixture::encode(value);
    EditorRecoverySnapshot decoded;
    core::pmr::String diagnostic;
    assert(EditorRecoveryCodec::decode(yaml, decoded, diagnostic)
        && decoded.project->metadata->fileBytes == metadata
        && ProjectInputSnapshot::equal(decoded.project->inputFiles, value.project->inputFiles));
    const auto reject = [&](auto modify) {
        auto node = Yaml::parseSingleDocument(yaml);
        modify(node);
        auto destination = decoded;
        destination.sequence = 99;
        assert(!EditorRecoveryCodec::decode(Yaml::serialize(node), destination, diagnostic)
            && destination.sequence == 99 && destination.project->metadata->fileBytes == metadata);
    };
    reject([](auto& node) {
        node["project"]["metadata"] = Node();
    });
    reject([](auto& node) {
        node["project"]["metadata"]["path"] = "other.yaml";
    });
    reject([](auto& node) {
        node["project"]["metadata"]["extra"] = true;
    });
    reject([](auto& node) {
        node["project"]["metadata"]["disk"]["bytes"] =
            static_cast<std::uint64_t>(EditorRecoveryCodec::maxMetadataBytes + 1);
    });
    reject([](auto& node) {
        node["project"]["inputs"][0]["fingerprint"] = "not-sha256";
    });
    reject([](auto& node) {
        node["project"]["inputs"][0]["path"] = "../escape";
    });
    reject([](auto& node) {
        node["project"]["inputs"][0]["extra"] = true;
    });
    reject([](auto& node) {
        node["project"]["inputs"].template get_value_ref<Node::sequence_type&>().push_back(
            node["project"]["inputs"][0]);
    });
    reject([](auto& node) {
        node["project"].template get_value_ref<Node::mapping_type&>().erase(Node("inputs"));
    });
    std::puts(
        "PASS recovery v2 exact metadata/input round-trip, strict required fields, path/hash/order/budget rejection and destination preservation");
}

void versionMigration(const RecoveryFixture& fixture)
{
    auto value = fixture.snapshot();
    core::pmr::String diagnostic, migrated;
    auto legacy = Yaml::parseSingleDocument(RecoveryFixture::encode(value));
    legacy["contractVersion"] = 1;
    EditorRecoverySnapshot destination;
    assert(!EditorRecoveryCodec::decode(Yaml::serialize(legacy), destination, diagnostic));
    assert(EditorRecoveryCodec::migrateVersion1(Yaml::serialize(legacy), migrated, diagnostic));
    assert(EditorRecoveryCodec::decode(migrated, destination, diagnostic));
    auto manifest = Yaml::parseSingleDocument(
        tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n"));
    value.project.emplace();
    value.project->id = Yaml::text(manifest, "id");
    value.project->savedFileBytes = value.project->draftYaml = Yaml::serialize(manifest);
    auto asLegacy = [&] {
        auto node = Yaml::parseSingleDocument(RecoveryFixture::encode(value));
        node["contractVersion"] = 1;
        auto& fields = node["project"].get_value_ref<Node::mapping_type&>();
        fields.erase(Node("metadata"));
        fields.erase(Node("inputs"));
        return node;
    };
    legacy = asLegacy();
    assert(EditorRecoveryCodec::migrateVersion1(Yaml::serialize(legacy), migrated, diagnostic));
    const auto untouched = migrated;
    manifest["editorMetadata"] = "editor.yaml";
    value.project->savedFileBytes = value.project->draftYaml = Yaml::serialize(manifest);
    // Construct old wire data directly: v1 never held the metadata bytes.
    legacy["project"]["draft"]["data"] = core::Base64::encode(
        core::ByteBuffer(value.project->draftYaml.begin(), value.project->draftYaml.end()));
    legacy["project"]["draft"]["bytes"] =
        static_cast<std::uint64_t>(value.project->draftYaml.size());
    legacy["project"]["disk"] = legacy["project"]["draft"];
    assert(!EditorRecoveryCodec::migrateVersion1(Yaml::serialize(legacy), migrated, diagnostic)
        && migrated == untouched);
    manifest["editorMetadata"] = Node();
    auto module = Node::mapping();
    module["id"] = "gameplay";
    module["resourceId"] = "4305ee93-3897-43ae-93da-ecbb0c569c84";
    module["source"] = "gameplay.arp";
    module["schemaVersion"] = 1;
    module["dependencies"] = Node::sequence();
    manifest["scriptModules"].get_value_ref<Node::sequence_type&>().push_back(module);
    value.project->savedFileBytes = value.project->draftYaml = Yaml::serialize(manifest);
    legacy = asLegacy();
    assert(!EditorRecoveryCodec::migrateVersion1(Yaml::serialize(legacy), migrated, diagnostic)
        && migrated == untouched && diagnostic == "EDITOR-RECOVERY-V1-EXTERNAL-INPUTS-UNBOUND");
    auto invalid = Yaml::parseSingleDocument(RecoveryFixture::encode(fixture.snapshot()));
    invalid["contractVersion"] = 99;
    assert(!EditorRecoveryCodec::migrateVersion1(Yaml::serialize(invalid), migrated, diagnostic)
        && migrated == untouched);
    std::puts(
        "PASS explicit v1 migration only for proven input-free records; normal decode rejects v1 and migration preserves destination on failure");
}

void smoke(const RecoveryFixture& fixture)
{
    const auto value = fixture.snapshot();
    const auto yaml = RecoveryFixture::encode(value);
    const core::pmr::Vector<std::uint8_t> bytes(yaml.begin(), yaml.end());
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            core::io::AtomicFileService files(1u, EditorRecoveryBudget{}.maxEnvelopeBytes);
            const auto path = core::FilePath::utf8(fixture.root / "recovery-slot.yaml");
            const auto saved = RecoveryFixture::finish(files, files.replace(path, bytes));
            if (!saved.success || !saved.committed)
                std::fprintf(stderr,
                    "recovery codec write cycle=%u path=%s success=%d committed=%d: %s\n",
                    cycle,
                    path.c_str(),
                    saved.success,
                    saved.committed,
                    saved.diagnostic.c_str());
            assert(saved.success && saved.committed);
            const auto read = RecoveryFixture::finish(files, files.read(path));
            assert(read.success && read.bytes == bytes);
            EditorRecoverySnapshot decoded;
            core::pmr::String diagnostic;
            // File workers only move bytes. Reflection validation occurs here
            // on the sole owner thread, without concurrent Inspector/encoding.
            assert(EditorRecoveryCodec::decode(
                {reinterpret_cast<const char*>(read.bytes.data()), read.bytes.size()},
                decoded,
                diagnostic));
            assert(decoded.scripts[0].savedFileBytes == value.scripts[0].savedFileBytes);
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    assert(!std::filesystem::exists(fixture.root / "authoring.arp.yaml")
        && !std::filesystem::exists(fixture.root / "rules.arp"));
    std::puts(
        "PASS recovery codec/file smoke: 20 warmed ordinary read/write/decode/destroy cycles; formal files untouched; core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    RecoveryFixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(fixture);
    else {
        roundTrip(fixture);
        invalidRecords(fixture);
        budgetContract(fixture);
        externalInputFields(fixture);
        versionMigration(fixture);
    }
}

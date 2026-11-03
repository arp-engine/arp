#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/project/project_file_save_task.h"
#include "editor/project/project_manifest.h"
#include "editor/project/project_source_session.h"
#include "editor/workspace/editor_recovery_codec.h"
#include "editor/workspace/editor_recovery_preparation.h"
#include "editor/workspace/editor_recovery_session.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/script_document_session.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace core::io;
using Yaml = core::YamlContract;

EditorRecoveryBudget budget()
{
    EditorRecoveryBudget value;
    value.maxEnvelopeBytes = 256 * 1024;
    value.maxPayloadBytes = 128 * 1024;
    value.maxScripts = 2;
    return value;
}

EditorRecoverySnapshot snapshot()
{
    EditorRecoverySnapshot value;
    value.sessionId = "4305ee93-3897-43ae-93da-ecbb0c569c83";
    value.sequence = 1;
    value.workspaceEpoch = 7;
    value.workspace = "Script";
    value.activeScript = 0;
    value.source.emplace();
    value.source->alias = "workspace";
    value.source->root = "nested";
    value.project.emplace();
    ProjectManifest manifest;
    manifest.id = "00000000-0000-4000-8000-000000000094";
    manifest.name = "Source recovery project";
    manifest.engineContract = "arp.scene-asset/2";
    ProjectDiagnostic issue;
    assert(ProjectManifestCodec::encode(manifest, value.project->savedFileBytes, issue));
    value.project->id = manifest.id;
    value.source->manifestFileBytes =
        core::pmr::String("\xef\xbb\xbf") + value.project->savedFileBytes;
    manifest.name = "Unsaved recovery draft";
    assert(ProjectManifestCodec::encode(manifest, value.project->draftYaml, issue));
    core::pmr::String diagnostic;
    ProjectInputSnapshot inputs({});
    assert(inputs.recordLogical("project.arp.yaml", value.source->manifestFileBytes, diagnostic));
    value.source->inputs = inputs.takeBindings();
    EditorSceneDocument document(false);
    document.scene().setName("Source recovery scene");
    assert(document.createObject("Recovered source object").value() != 0);
    assert(EditorSceneAuthoringCodec::encode(document, value.scene.yaml, diagnostic));
    value.scene.savedFileBytes = value.scene.yaml;
    value.scene.path = value.scene.boundTarget = "scene.yaml";
    assert(
        EditorSceneAuthoringCodec::contentHash(document, value.scene.savedContentHash, diagnostic));
    EditorRecoveryScript script;
    script.path = "rules.arp";
    script.savedFileBytes = "\xef\xbb\xbf# saved\r\n";
    script.savedText = ScriptDocumentSession::normalizeFileBytes(script.savedFileBytes);
    script.text = "# unsaved source edit\n";
    script.newline = "\r\n";
    script.cursor = 4;
    script.anchor = 1;
    value.scripts.push_back(std::move(script));
    return value;
}

void contracts()
{
    auto value = snapshot();
    core::pmr::String yaml, diagnostic;
    EditorRecoverySession nativeSession;
    assert(nativeSession.submit(value, EditorRecoverySession::TimePoint{})
        == EditorRecoverySubmitStatus::Rejected);
    assert(nativeSession.result().diagnostic == "EDITOR-RECOVERY-SOURCE-SESSION-REQUIRED"
        && nativeSession.directory().empty());
    unified_io_fixture::Workspace nativeRoot;
    assert(!EditorRecoveryPreparation::prepare(value, nativeRoot.root, diagnostic, budget()));
    assert(diagnostic == "EDITOR-RECOVERY-SOURCE-PREPARATION-REQUIRED");
    assert(EditorRecoveryCodec::encode(value, yaml, diagnostic, budget()));
    const auto root = Yaml::parseSingleDocument(yaml);
    assert(Yaml::uint32(root, "contractVersion") == EditorRecoveryCodec::sourceContractVersion);
    assert(Yaml::required(root, "projectRoot").is_null());
    assert(Yaml::uint32(Yaml::required(root, "source"), "contractVersion") == 1);
    EditorRecoverySnapshot decoded;
    assert(EditorRecoveryCodec::decode(yaml, decoded, diagnostic, budget()));
    assert(decoded.projectRoot.empty() && decoded.source && decoded.project);
    assert(decoded.source->alias == "workspace" && decoded.source->root == "nested"
        && decoded.source->manifestFileBytes == value.source->manifestFileBytes
        && ProjectInputSnapshot::equal(decoded.source->inputs, value.source->inputs)
        && decoded.project->savedFileBytes == value.project->savedFileBytes
        && decoded.scripts.front().savedFileBytes == value.scripts.front().savedFileBytes);
    core::pmr::String repeated;
    assert(
        EditorRecoveryCodec::encode(decoded, repeated, diagnostic, budget()) && repeated == yaml);
    const auto reject = [&](EditorRecoverySnapshot changed) {
        core::pmr::String output = "unchanged";
        assert(!EditorRecoveryCodec::encode(changed, output, diagnostic, budget()));
        assert(output == "unchanged" && !diagnostic.empty());
    };
    auto changed = value;
    changed.projectRoot = "/absolute/native/root";
    reject(changed);
    changed = value;
    changed.source->alias = "workspace/other";
    reject(changed);
    changed = value;
    changed.source->root = "../escape";
    reject(changed);
    changed = value;
    changed.source->root = "C:/host/path";
    reject(changed);
    changed = value;
    changed.project.reset();
    reject(changed);
    changed = value;
    changed.source->manifestFileBytes += "\n# changed raw baseline\n";
    reject(changed);
    changed = value;
    changed.source->inputs.front().fingerprint = core::Sha256::hexDigest("other");
    reject(changed);
    changed = value;
    changed.source->inputs.push_back(changed.source->inputs.front());
    reject(changed);
    changed = value;
    changed.project->inputFiles.push_back({"missing.arp", 1, core::Sha256::hexDigest("x")});
    reject(changed);
    changed = value;
    changed.scene.boundTarget = "different.yaml";
    reject(changed);
    changed = value;
    changed.scripts.front().savedFileBytes.push_back('\0');
    reject(changed);
    changed = value;
    changed.scripts.front().text = core::pmr::String("\xc0\x80", 2);
    reject(changed);
    const auto rejectWire = [&](Yaml::Node modified) {
        const auto oldSession = decoded.sessionId;
        assert(
            !EditorRecoveryCodec::decode(Yaml::serialize(modified), decoded, diagnostic, budget()));
        assert(decoded.sessionId == oldSession && decoded.source->alias == "workspace");
    };
    auto wire = root;
    wire["contractVersion"] = 2;
    rejectWire(wire);
    wire = root;
    wire["source"]["contractVersion"] = 99;
    rejectWire(wire);
    wire = root;
    wire["source"]["mount"] = 17;
    rejectWire(wire);
    wire = root;
    wire["projectRoot"] = "/not/a/source/root";
    rejectWire(wire);
    auto tiny = budget();
    tiny.maxPayloadBytes = 1;
    assert(!EditorRecoveryCodec::decode(yaml, decoded, diagnostic, tiny));
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope fault(quota);
        assert(!EditorRecoveryCodec::decode(yaml, decoded, diagnostic, budget()));
    }
    assert(quota->liveBytes() == 0 && decoded.source->alias == "workspace");
    // A mount-root source is represented explicitly by an empty logical root.
    value.source->root.clear();
    assert(EditorRecoveryCodec::encode(value, yaml, diagnostic, budget()));
    assert(EditorRecoveryCodec::decode(yaml, decoded, diagnostic, budget()));
    assert(decoded.source->root.empty() && decoded.projectRoot.empty());
}

void persistence(bool native)
{
    unified_io_fixture::Workspace workspace;
    auto value = snapshot();
    workspace.write("nested/project.arp.yaml", value.source->manifestFileBytes);
    workspace.write("nested/scene.yaml", value.scene.savedFileBytes);
    workspace.write("nested/rules.arp", value.scripts.front().savedFileBytes);
    const auto limits = budget();
    core::SharedPtr<ManagedFileBackend> backend;
    if (native) {
        auto desc = workspace.descriptor();
        desc.allowObservedBaselineReplace = true;
        desc.ioChunkBytes = 4096;
        desc.maxBytesInFlight = 4 * limits.maxEnvelopeBytes;
        backend = core::makeShared<NativeDirectoryFileBackend>(desc);
    } else {
        auto memory = core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{
            5, 2 * limits.maxEnvelopeBytes, 1, 4 * limits.maxEnvelopeBytes, 1024, true});
        assert(memory->registerDirectory("nested") == FileIoStatus::Success);
        const auto registerFile = [&](const char* path, const core::pmr::String& text) {
            assert(memory->registerFile(
                       path, reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
                == FileIoStatus::Success);
        };
        registerFile("nested/project.arp.yaml", value.source->manifestFileBytes);
        registerFile("nested/scene.yaml", value.scene.savedFileBytes);
        registerFile("nested/rules.arp", value.scripts.front().savedFileBytes);
        backend = std::move(memory);
    }
    core::runtime::CompletionQueue queue({8});
    MountedFileBackend mounts({1, 1, 4 * limits.maxEnvelopeBytes, 32, 1024});
    const auto mount = mounts.mount("workspace", backend);
    assert(mount);
    FileService files(queue, mounts);
    // Source provenance comes from actual IO here; the serialized record does
    // not carry or reuse either this mount generation or its runtime epoch.
    ProjectSourceSession inputs(files, {mount, "nested", 9}, {1, 4096, 4096});
    assert(inputs.read("project.arp.yaml", 4096) == FileIoStatus::Accepted);
    unified_io_fixture::wait(queue, [&] {
        return inputs.state() != ProjectSourceState::Pending;
    });
    const auto scope = inputs.finish(9);
    assert(scope && ProjectInputSnapshot::equal(scope->identities(), value.source->inputs));
    value.source->inputs = scope->identities();
    core::pmr::String yaml, diagnostic;
    assert(EditorRecoveryCodec::encode(value, yaml, diagnostic, limits));
    FileSaveRequest request;
    request.pathScope = FilePathScope::MountedResource;
    request.mount = mount;
    request.path = "nested/recovery.yaml";
    request.bytes.assign(yaml.begin(), yaml.end());
    request.maxWriteBytes = limits.maxEnvelopeBytes;
    request.precondition = FileSavePrecondition::MustBeMissing;
    ProjectFileSaveTask write(files, std::move(request));
    assert(!write.submitted());
    write.advance();
    unified_io_fixture::wait(queue, [&] {
        return write.terminal();
    });
    assert(write.result().commitState == FileCommitState::Committed);
    ProjectSourceSession read(
        files, {mount, "nested", 10}, {1, limits.maxEnvelopeBytes, limits.maxEnvelopeBytes});
    assert(read.read("recovery.yaml",
               limits.maxEnvelopeBytes,
               write.result().committedVersion,
               limits.maxEnvelopeBytes)
        == FileIoStatus::Accepted);
    unified_io_fixture::wait(queue, [&] {
        return read.state() != ProjectSourceState::Pending;
    });
    const auto acquired = read.finish(10);
    assert(acquired);
    const auto& raw = acquired->find("recovery.yaml")->receipt.bytes;
    EditorRecoverySnapshot decoded;
    assert(EditorRecoveryCodec::decode(
        {reinterpret_cast<const char*>(raw.data()), raw.size()}, decoded, diagnostic, limits));
    assert(decoded.projectRoot.empty() && decoded.source->alias == "workspace"
        && decoded.workspaceEpoch == 7
        && decoded.source->manifestFileBytes == value.source->manifestFileBytes
        && decoded.scripts.front().text == "# unsaved source edit\n");
    value.sequence = 2;
    value.workspaceEpoch = 8;
    assert(EditorRecoveryCodec::encode(value, yaml, diagnostic, limits));
    const char* issue = nullptr;
    auto replacement = acquired->prepareFileSave(
        files, 10, "recovery.yaml", {yaml.begin(), yaml.end()}, limits.maxEnvelopeBytes, issue);
    assert(replacement && !issue);
    ProjectFileSaveTask update(files, replacement->takeRequest());
    update.advance();
    unified_io_fixture::wait(queue, [&] {
        return update.terminal();
    });
    const auto current = replacement->finish(files, 10, update.result());
    assert(current);
    const auto& updated = current->find("recovery.yaml")->receipt.bytes;
    assert(
        EditorRecoveryCodec::decode({reinterpret_cast<const char*>(updated.data()), updated.size()},
            decoded,
            diagnostic,
            limits));
    assert(decoded.sequence == 2 && decoded.workspaceEpoch == 8);
    auto stale = acquired->prepareFileSave(
        files, 10, "recovery.yaml", {raw.begin(), raw.end()}, limits.maxEnvelopeBytes, issue);
    assert(stale);
    ProjectFileSaveTask conflict(files, stale->takeRequest());
    conflict.advance();
    unified_io_fixture::wait(queue, [&] {
        return conflict.terminal();
    });
    assert(conflict.result().status == FileIoStatus::VersionMismatch
        && conflict.result().commitState == FileCommitState::NotCommitted);
    mounts.shutdown();
    queue.dispatch(8);
    assert(mounts.telemetry().inFlight == 0 && queue.pending() == 0 && queue.reserved() == 0);
}
} // namespace

int main(int argc, char** argv)
{
    const bool native = argc > 1 && std::string_view(argv[1]) == "--native";
    const unsigned loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    contracts();
    persistence(native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i) {
        contracts();
        persistence(native);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Source recovery envelope %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}

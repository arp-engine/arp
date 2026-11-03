#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/tests/fixtures/project_source_acquisition_fixture.h"
#include "editor/tests/fixtures/script_scene_shell_fixture.h"
#include "editor/workspace/editor_source_recovery_preparation.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace core::io;

constexpr std::uint64_t epoch = 41;
constexpr ProjectInputBudget limits{16, 1024 * 1024, 4 * 1024 * 1024};

void checkAt(int line, bool value, const core::pmr::String& diagnostic = {})
{
    if (!value)
        std::fprintf(stderr, "Source recovery check at line %d: %s\n", line, diagnostic.c_str());
    assert(value);
}

#define check(...) checkAt(__LINE__, __VA_ARGS__)

// Real producer files and memory/native IO share the same logical mount root.
// The initializer/constructor in this fixture must never execute during restore.
class FixtureHost final {
public:
    explicit FixtureHost(bool native)
    {
        std::filesystem::create_directories(source.files.root / "logical");
        auto memory = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{8, limits.maxTotalBytes, 1, limits.maxTotalBytes, 512, false});
        check(memory->registerDirectory("logical") == FileIoStatus::Success);
        for (const auto* name : {"project.arp.yaml", "primary.arp", "secondary.arp"}) {
            const auto bytes = std::string_view(name) == "project.arp.yaml"
                ? core::pmr::String("\xef\xbb\xbf") + source.read(name)
                : source.read(name);
            source.files.write(core::pmr::String("logical/") + name, bytes);
            check(memory->registerFile(core::pmr::String("logical/") + name,
                      reinterpret_cast<const std::uint8_t*>(bytes.data()),
                      bytes.size())
                == FileIoStatus::Success);
        }
        if (native) {
            NativeDirectoryFileBackendDesc desc;
            desc.rootPath = core::FilePath::utf8(source.files.root);
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = limits.maxTotalBytes;
            desc.maxPathBytes = 1024;
            desc.ioChunkBytes = 4096;
            desc.directoryBufferBytes = 4096;
            desc.maxScanEntries = 32;
            desc.maxScanNameBytes = 4096;
            backend = core::makeShared<NativeDirectoryFileBackend>(desc);
        } else {
            backend = memory;
        }
        mount = mounts.mount("workspace", backend);
        check(bool(mount));
        const auto acquired = editor::tests::ProjectSourceAcquisitionFixture::acquire(
            files, queue, location(), limits, limits.maxTotalBytes);
        EditorProjectResources resources;
        const auto loaded = resources.loadSources(acquired, files, epoch);
        check(loaded.success, loaded.diagnostics);
        value.sessionId = "4305ee93-3897-43ae-93da-ecbb0c569c83";
        value.sequence = 1;
        value.workspaceEpoch = 7;
        value.workspace = "3D";
        value.source.emplace();
        value.source->alias = "workspace";
        value.source->root = "logical";
        value.source->manifestFileBytes = core::pmr::String("\xef\xbb\xbf") + source.manifest;
        value.source->inputs = acquired->identities();
        value.project.emplace();
        value.project->id = source.files.manifest.id;
        value.project->savedFileBytes = source.manifest;
        auto draft = source.files.manifest;
        draft.name = "Recovered settings draft";
        ProjectDiagnostic diagnostic;
        check(ProjectManifestCodec::encode(draft, value.project->draftYaml, diagnostic));
        value.project->inputFiles = resources.inputBindings();
        value.project->packages = resources.packageBindings();
        value.scene.yaml = value.scene.savedFileBytes = source.original;
        value.scene.path = value.scene.boundTarget = "scene.yaml";
        EditorSceneDocument document(false);
        core::pmr::String issue;
        check(
            document.bindServiceSession(core::makeShared<EditorSceneServiceSession>(
                                            resources.retainManager(), resources.physicsWorld2D()),
                issue),
            issue);
        const auto logicalRoot = std::filesystem::u8path("logical");
        auto inputs = EditorSceneScriptPolicy::inputs(logicalRoot, source.files.manifest);
        ProjectSourceContext context{acquired, &files, epoch};
        inputs.sources = &context;
        check(EditorSceneScriptCodec::decode(source.original, document, inputs, issue), issue);
        check(EditorSceneScriptCodec::observeContentHash(
                  document, inputs, value.scene.savedContentHash, issue),
            issue);
        document.scene().setName("Recovered unsaved scene");
        check(EditorSceneScriptCodec::encode(document, inputs, value.scene.yaml, issue), issue);
    }

    ~FixtureHost()
    {
        mounts.shutdown();
        while (queue.pending())
            queue.dispatch(8);
        check(queue.reserved() == 0 && mounts.telemetry().inFlight == 0);
    }

    ProjectSourceLocation location() const { return {mount, "logical", epoch}; }

    void finish(EditorSourceRecoveryPreparation& task)
    {
        unified_io_fixture::wait(queue, [&] {
            task.advance(epoch);
            return task.state() != SourceRecoveryState::Pending;
        });
    }

    ScriptSceneShellFixture source;
    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{2, 1, limits.maxTotalBytes, 32, 1024}};
    FileService files{queue, mounts};
    core::SharedPtr<ManagedFileBackend> backend;
    core::PoolHandle mount;
    EditorRecoverySnapshot value;
};

void successful(FixtureHost& host)
{
    const auto submitted = host.files.telemetry().submitted;
    EditorSourceRecoveryPreparation task(
        host.files, host.value, "workspace", host.location(), limits);
    check(task.state() == SourceRecoveryState::Pending,
        core::pmr::String(task.diagnostic()) + ": " + core::pmr::String(task.diagnosticDetail()));
    check(host.files.telemetry().submitted == submitted);
    host.finish(task);
    check(task.state() == SourceRecoveryState::Ready, core::pmr::String(task.diagnostic()));
    const char* issue = nullptr;
    const auto* candidate = task.candidate(epoch, issue);
    check(candidate && !issue);
    check(candidate->draft.name == "Recovered settings draft");
    check(candidate->document->scene().name() == "Recovered unsaved scene");
    check(candidate->sources.epoch == epoch && candidate->submitted.workspaceEpoch == 7);
    check(ProjectInputSnapshot::equal(
        candidate->sources.snapshot->identities(), host.value.source->inputs));
    const auto* body = candidate->document->scene().findComponent(host.source.bodyId);
    const auto* child = candidate->document->scene().findObject(host.source.child);
    const auto* link = child ? child->findComponent(host.source.linkId) : nullptr;
    const auto* mover = candidate->document->scene().findComponent(host.source.scriptId);
    check(body && link && mover);
    check(link->scriptInstance().ptr<reflection::instance>()->slot("body")
        == body->nativeReflectionObject());
    check(mover->scriptInstance().ptr<reflection::instance>()->slot("amount").cast<int>() == 7);
    check(!task.candidate(epoch + 1, issue) && issue);
    task.cancel();
    check(task.state() == SourceRecoveryState::Ready);
}

void rejected(FixtureHost& host)
{
    const auto submitted = host.files.telemetry().submitted;
    EditorSourceRecoveryPreparation alias(host.files, host.value, "other", host.location(), limits);
    check(alias.state() == SourceRecoveryState::Failed);
    auto location = host.location();
    location.root = "different";
    EditorSourceRecoveryPreparation root(host.files, host.value, "workspace", location, limits);
    check(root.state() == SourceRecoveryState::Failed);
    EditorSourceRecoveryPreparation tiny(
        host.files, host.value, "workspace", host.location(), {1, 8, 8});
    check(tiny.state() == SourceRecoveryState::Failed);
    auto record = host.value;
    record.source.reset();
    EditorSourceRecoveryPreparation mixed(host.files, record, "workspace", host.location(), limits);
    check(mixed.state() == SourceRecoveryState::Failed);
    EditorSourceRecoveryPreparation stale(
        host.files, host.value, "workspace", host.location(), limits);
    stale.advance(epoch + 1);
    check(stale.state() == SourceRecoveryState::Failed);
    EditorSourceRecoveryPreparation cancelled(
        host.files, host.value, "workspace", host.location(), limits);
    cancelled.cancel();
    cancelled.advance(epoch);
    check(cancelled.state() == SourceRecoveryState::Cancelled);
    check(host.files.telemetry().submitted == submitted);
    auto structuralDraft = host.source.files.manifest;
    structuralDraft.scriptModules.front().authoring->maxInstances += 1;
    ProjectDiagnostic projectDiagnostic;
    record = host.value;
    check(ProjectManifestCodec::encode(
        structuralDraft, record.project->draftYaml, projectDiagnostic));
    EditorSourceRecoveryPreparation dependency(
        host.files, record, "workspace", host.location(), limits);
    check(dependency.state() == SourceRecoveryState::Failed);
    check(dependency.diagnostic() == "EDITOR-RECOVERY-PROJECT-DRAFT-CONTRACT");
    check(host.files.telemetry().submitted == submitted);
    record = host.value;
    record.scene.savedContentHash.back() = record.scene.savedContentHash.back() == '0' ? '1' : '0';
    EditorSourceRecoveryPreparation hash(host.files, record, "workspace", host.location(), limits);
    host.finish(hash);
    check(hash.state() == SourceRecoveryState::Failed);
    check(hash.diagnostic() == "EDITOR-RECOVERY-SCENE-SAVEPOINT-MISMATCH");
    // Missing declared dependency cannot fall through to the physical fixture.
    record = host.value;
    record.source->inputs.erase(record.source->inputs.begin());
    record.project->inputFiles.erase(record.project->inputFiles.begin());
    EditorSourceRecoveryPreparation missing(
        host.files, record, "workspace", host.location(), limits);
    host.finish(missing);
    check(missing.state() == SourceRecoveryState::Failed);
    // A valid record with a forged strong identity must fail after actual IO.
    record = host.value;
    auto& identity = record.source->inputs.front();
    identity.fingerprint.back() = identity.fingerprint.back() == '0' ? '1' : '0';
    record.project->inputFiles.front().fingerprint = identity.fingerprint;
    EditorSourceRecoveryPreparation conflict(
        host.files, record, "workspace", host.location(), limits);
    host.finish(conflict);
    check(conflict.state() == SourceRecoveryState::Failed);
    check(conflict.diagnostic() == "EDITOR-RECOVERY-SOURCE-INPUT-CONFLICT");
    // Reading a complete dependency set is insufficient if recorded provenance
    // omits a resource loader input. No candidate may escape that mismatch.
    record = host.value;
    record.project->inputFiles.clear();
    EditorSourceRecoveryPreparation closure(
        host.files, record, "workspace", host.location(), limits);
    host.finish(closure);
    check(closure.state() == SourceRecoveryState::Failed);
    check(closure.diagnostic() == "EDITOR-RECOVERY-SOURCE-RESOURCE-CLOSURE");
    EditorSourceRecoveryPreparation fault(
        host.files, host.value, "workspace", host.location(), limits);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope denied(quota);
        fault.advance(epoch);
    }
    check(fault.state() == SourceRecoveryState::Failed && quota->liveBytes() == 0);
    EditorSourceRecoveryPreparation during(
        host.files, host.value, "workspace", host.location(), limits);
    during.advance(epoch);
    during.cancel();
    check(during.state() == SourceRecoveryState::Cancelled);
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0 && host.queue.reserved() == 0;
    });
    // Destroying an accepted task cancels it without callback access to the task.
    {
        EditorSourceRecoveryPreparation pending(
            host.files, host.value, "workspace", host.location(), limits);
        pending.advance(epoch);
        check(pending.state() == SourceRecoveryState::Pending);
    }
    unified_io_fixture::wait(host.queue, [&] {
        return host.mounts.telemetry().inFlight == 0 && host.queue.reserved() == 0;
    });
}

void plainScene(FixtureHost& host)
{
    auto record = host.value;
    EditorSceneDocument document(false);
    document.scene().setName("Recovered native scene");
    check(document.createObject("Cold native object").value() != 0);
    core::pmr::String issue;
    check(EditorSceneAuthoringCodec::encode(document, record.scene.savedFileBytes, issue), issue);
    check(EditorSceneAuthoringCodec::contentHash(document, record.scene.savedContentHash, issue),
        issue);
    document.scene().setName("Unsaved native draft");
    check(EditorSceneAuthoringCodec::encode(document, record.scene.yaml, issue), issue);
    EditorSourceRecoveryPreparation task(host.files, record, "workspace", host.location(), limits);
    host.finish(task);
    check(task.state() == SourceRecoveryState::Ready, core::pmr::String(task.diagnostic()));
    const char* diagnostic = nullptr;
    const auto* candidate = task.candidate(epoch, diagnostic);
    check(candidate && candidate->document->scene().name() == "Unsaved native draft");
    // Unsaved/unbound scenes have no disk baseline to synthesize.
    record.scene.path.clear();
    record.scene.boundTarget.clear();
    record.scene.savedContentHash.clear();
    record.scene.savedFileBytes.clear();
    EditorSourceRecoveryPreparation unsaved(
        host.files, record, "workspace", host.location(), limits);
    host.finish(unsaved);
    check(unsaved.state() == SourceRecoveryState::Ready, core::pmr::String(unsaved.diagnostic()));
}

void generation(FixtureHost& host)
{
    EditorSourceRecoveryPreparation task(
        host.files, host.value, "workspace", host.location(), limits);
    host.finish(task);
    check(task.state() == SourceRecoveryState::Ready, core::pmr::String(task.diagnostic()));
    check(host.mounts.unmount(host.mount));
    const auto newer = host.mounts.mount("workspace", host.backend);
    check(bool(newer) && !(newer == host.mount));
    const char* issue = nullptr;
    check(!task.candidate(epoch, issue) && issue);
    host.mount = newer;
}
} // namespace

int main(int argc, char** argv)
{
    const bool native = argc > 1 && std::string_view(argv[1]) == "--native";
    const unsigned loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20 : 1;
    {
        FixtureHost warm(native);
        successful(warm);
        plainScene(warm);
        rejected(warm);
        generation(warm);
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i) {
        FixtureHost host(native);
        successful(host);
        plainScene(host);
        rejected(host);
        generation(host);
    }
    const auto after = core::defaultCpuAllocator().stats();
    check(before.liveBytes == after.liveBytes);
    check(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf(
        "Source recovery preparation %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}

#include "editor/tests/fixtures/project_fixture.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_recovery_admission.h"
#include "editor/workspace/editor_recovery_preparation.h"
#include "editor/workspace/editor_shell.h"

namespace {
using namespace arp;
using namespace arp::editor;
using Admission = EditorRecoveryAdmission;
using Preparation = EditorRecoveryPreparation;
using tests::RecoveryFixture;

class Fixture final {
public:
    Fixture()
    {
        RecoveryFixture::write(files.root / "first.arp", "# first\r\n");
        RecoveryFixture::write(files.root / "second.arp", "# second\n");
    }

    void initialize(EditorShell& shell) const
    {
        assert(shell
                .load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui",
                    files.root)
                .ok());
    }

    static core::UniquePtr<Admission::Ticket> ticket(EditorShell& shell, bool discard = true)
    {
        core::pmr::String diagnostic;
        auto value = Admission::capture(shell, discard, diagnostic);
        if (!value)
            std::fprintf(stderr, "admission capture: %s\n", diagnostic.c_str());
        assert(value && diagnostic.empty() && value->ownerAlive());
        assert(Admission::validate(shell, *value, diagnostic) && diagnostic.empty());
        return value;
    }

    static void stale(EditorShell& shell, const Admission::Ticket& ticket)
    {
        core::pmr::String diagnostic;
        const auto before = shell.stateVersion();
        assert(!Admission::validate(shell, ticket, diagnostic)
            && diagnostic.find("EDITOR-RECOVERY-LIVE-") == 0 && shell.stateVersion() == before);
    }

    RecoveryFixture files;
};

void sceneFreshness(Fixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    auto ticket = Fixture::ticket(shell, false);
    const auto version = shell.stateVersion();
    assert(!shell.execute(SetSceneObjectNameCommand("")).success);
    core::pmr::String diagnostic;
    assert(Admission::validate(shell, *ticket, diagnostic));
    const auto name =
        shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject())->name();
    assert(shell.execute(SetSceneObjectNameCommand(name)).success);
    assert(Admission::validate(shell, *ticket, diagnostic));
    // The public native document entry does not increment dispatcher version.
    assert(shell.sceneDocument().renameSelected("direct edit"));
    assert(shell.stateVersion() == version);
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    const auto selected = shell.sceneDocument().selectedObject();
    assert(shell.execute(SetSceneObjectNameCommand("historical edit")).success);
    assert(shell.execute(UndoSceneCommand{}).success && shell.stateVersion() == version);
    assert(shell.sceneDocument().scene().findObject(selected)->name() == "direct edit");
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    core::pmr::String before, after;
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), before, diagnostic));
    assert(shell.execute(RedoSceneCommand{}).success && shell.execute(UndoSceneCommand{}).success);
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic)
        && before == after && shell.stateVersion() == version);
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    assert(!shell.sceneDocument().replaceWith(std::move(shell.sceneDocument())));
    assert(Admission::validate(shell, *ticket, diagnostic));
    assert(EditorSceneAuthoringCodec::decode(
        before, shell.sceneDocument(), nullptr, nullptr, diagnostic));
    assert(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, diagnostic)
        && before == after && shell.stateVersion() == version);
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    shell.sceneDocument().setResourceManagerProvider(
        shell.sceneDocument().resourceManagerProvider());
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    shell.sceneDocument().setPhysicsWorld2DProvider(shell.sceneDocument().physicsWorld2DProvider());
    Fixture::stale(shell, *ticket);
    std::puts(
        "PASS direct edits, unchanged/failed commands, exact-content Undo/Redo, same-content Scene replacement and provider ownership");
}

void scriptFreshness(Fixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    auto ticket = Fixture::ticket(shell, false);
    const auto version = shell.stateVersion();
    assert(shell.openScriptFile(fixture.files.root / "first.arp"));
    assert(shell.stateVersion() == version);
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell, false);
    const auto id = shell.activeScriptDocumentId();
    assert(shell.execute(ReplaceScriptTextCommand(id, "# local\n")).success);
    assert(shell.execute(UndoScriptCommand{id}).success && !shell.scriptDirty());
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell, false);
    assert(shell.execute(RedoScriptCommand{id}).success
        && shell.execute(UndoScriptCommand{id}).success && shell.stateVersion() == version);
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    // Simulate a native mutable-buffer caller, without dispatcher/session hooks.
    auto& text = const_cast<core::pmr::String&>(shell.scriptText());
    text[0] = '!';
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    assert(shell.openScriptFile(fixture.files.root / "second.arp"));
    Fixture::stale(shell, *ticket);
    assert(RecoveryFixture::read(fixture.files.root / "first.arp") == "# first\r\n");

    ScriptDocumentSession session;
    const auto open = session.openFile(fixture.files.root, fixture.files.root / "second.arp");
    assert(open && session.setActiveIndex(open.documentIndex));
    auto state = session.stateTicket();
    session.setActiveSelection(0, 0);
    session.setActiveScrollLine(0);
    assert(state.isCurrent() && !session.setActiveIndex(100) && state.isCurrent());
    session.setActiveSelection(1, 0);
    session.setActiveSelection(0, 0);
    assert(!state.isCurrent());
    state = session.stateTicket();
    session.setActiveScrollLine(2);
    session.setActiveScrollLine(0);
    assert(!state.isCurrent());
    state = session.stateTicket();
    assert(session.reloadActiveFile(fixture.files.root, false) && state.isCurrent());
    assert(session.saveActiveFile(fixture.files.root) && state.isCurrent());
    assert(session.replaceActiveRange(0, 0, "", ScriptDocumentSession::maxScriptBytes)
            == ScriptDocumentSession::EditResult::Unchanged
        && state.isCurrent());
    assert(!session.undoActive() && !session.redoActive() && state.isCurrent());
    std::puts(
        "PASS direct script open/text edits, history round trip, selection/scroll tickets and unchanged operation stability");
}

void gatesAndBudgets(Fixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    core::pmr::String diagnostic;
    Admission::Ticket empty;
    assert(!empty.ownerAlive() && !Admission::validate(shell, empty, diagnostic));
    auto ticket = Fixture::ticket(shell, false);
    assert(shell.execute(SetSceneObjectNameCommand("unsaved")).success);
    assert(!Admission::capture(shell, false, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-LIVE-UNSAVED");
    Fixture::stale(shell, *ticket);
    ticket = Fixture::ticket(shell);
    const auto object = shell.sceneDocument().selectedObject().value();
    assert(shell.execute(BeginSceneTransformEditCommand{object}).success);
    assert(!Admission::capture(shell, true, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-LIVE-INTERACTION-BUSY");
    Fixture::stale(shell, *ticket);
    assert(shell.execute(EndSceneTransformEditCommand{object}).success);
    ticket = Fixture::ticket(shell);
    SetGamePreviewStateCommand playing("running");
    playing.running = true;
    assert(shell.execute(playing).success);
    assert(!Admission::capture(shell, true, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-LIVE-WORKSPACE-BUSY");
    Fixture::stale(shell, *ticket);
    assert(shell.execute(SetGamePreviewStateCommand("stopped")).success);
    ticket = Fixture::ticket(shell);
    shell.activate("exit_ui_request");
    assert(!Admission::capture(shell, true, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-LIVE-INTERACTION-BUSY");
    Fixture::stale(shell, *ticket);
    shell.activate("exit_ui_cancel");
    const auto version = shell.stateVersion();
    assert(!Admission::capture(shell, true, diagnostic, {1, 4096})
        && diagnostic == "EDITOR-RECOVERY-LIVE-BYTE-BUDGET" && shell.stateVersion() == version);
    assert(!Admission::capture(shell, true, diagnostic, {129u * 1024u * 1024u, 4096})
        && diagnostic == "EDITOR-RECOVERY-LIVE-BUDGET-INVALID");
    assert(shell.openScriptFile(fixture.files.root / "first.arp"));
    assert(
        shell.execute(ReplaceScriptTextCommand(shell.activeScriptDocumentId(), "# edit")).success);
    assert(!Admission::capture(shell, true, diagnostic, {128u * 1024u * 1024u, 0})
        && diagnostic == "EDITOR-RECOVERY-LIVE-HISTORY-BUDGET");
    assert(shell.execute(RequestEditorExitCommand{true}).success && shell.exitRequested());
    assert(!Admission::capture(shell, true, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-LIVE-OWNER-INACTIVE");
    std::puts(
        "PASS dirty policy, gestures, Play, modal, exit and explicit observation budgets; no state changes on rejection");
}

void lifetimesAndCandidate(Fixture& fixture)
{
    core::UniquePtr<Admission::Ticket> retired;
    {
        EditorShell shell;
        fixture.initialize(shell);
        const auto manager = shell.retainProjectResourceManager();
        const auto references = manager.use_count();
        retired = Fixture::ticket(shell, false);
        assert(manager.use_count() == references);
    }
    assert(!retired->ownerAlive());
    EditorShell shell;
    fixture.initialize(shell);
    Fixture::stale(shell, *retired);
    core::pmr::String diagnostic;
    auto snapshot = fixture.files.snapshot();
    auto candidate = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic);
    if (!candidate)
        std::fprintf(stderr, "workspace preparation: %s\n", diagnostic.c_str());
    assert(candidate && candidate->recovery->scripts[0].id == 0
        && Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    RecoveryFixture::write(fixture.files.root / "rules.arp", "changed formal file");
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    std::filesystem::remove(fixture.files.root / "rules.arp");
    assert(Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    assert(shell.sceneDocument().renameSelected("change while candidate waits"));
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    assert(candidate->recovery->scripts[0].id == 0 && !shell.exitRequested());
    std::puts(
        "PASS weak owner lifetimes, cross-owner rejection, combined live/file candidate preparation and delayed validation");
}

void candidateContentBinding(Fixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    auto snapshot = fixture.files.snapshot();
    snapshot.scripts.push_back(snapshot.scripts[0]);
    snapshot.scripts[1].path = "another.arp";
    core::pmr::String diagnostic;
    auto candidate = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic);
    assert(candidate && Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    const auto version = shell.stateVersion();
    const auto changed = [&](auto& field, auto replacement) {
        auto original = field;
        field = std::move(replacement);
        assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
            && !diagnostic.empty() && shell.stateVersion() == version);
        field = std::move(original);
        assert(Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    };
    auto& prepared = *candidate->recovery;
    changed(prepared.submitted.sequence, std::uint64_t{99});
    changed(prepared.submitted.workspaceEpoch, std::uint64_t{99});
    changed(
        prepared.submitted.sessionId, core::pmr::String("5305ee93-3897-43ae-93da-ecbb0c569c83"));
    changed(prepared.submitted.workspace, core::pmr::String("3D"));
    changed(prepared.submitted.activeScript, std::optional<std::size_t>{1});
    changed(prepared.submitted.scripts[0].text, core::pmr::String("# different record\n"));
    auto& script = prepared.scripts[0];
    changed(script.text, core::pmr::String("# different candidate\n"));
    changed(script.savedText, core::pmr::String("# false savepoint\n"));
    changed(script.savedFileBytes, core::pmr::String("# false raw baseline\r\n"));
    changed(script.newline, core::pmr::String("\n"));
    changed(script.cursor, std::size_t{0});
    changed(script.anchor, std::size_t{0});
    changed(script.scrollLine, std::size_t{0});
    changed(script.path, fixture.files.root / "another.arp");
    changed(script.id, std::uint64_t{1});
    const auto originalRecordText = prepared.submitted.scripts[0].text;
    const auto originalCandidateText = script.text;
    prepared.submitted.scripts[0].text = script.text = "# coherent forged replacement\n";
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-CANDIDATE-CONTENT-CHANGED");
    prepared.submitted.scripts[0].text = originalRecordText;
    script.text = originalCandidateText;
    script.undo.emplace_back();
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    script.undo.clear();
    script.redo.emplace_back();
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    script.redo.clear();
    std::swap(prepared.scripts[0], prepared.scripts[1]);
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    std::swap(prepared.scripts[0], prepared.scripts[1]);
    changed(prepared.conflicts[0].kind, Preparation::ConflictKind::Changed);
    changed(prepared.conflicts[0].relativePath, core::pmr::String("another.arp"));
    changed(prepared.files[0].bytes, std::optional<core::pmr::String>{core::pmr::String{}});
    changed(prepared.files[0].target, fixture.files.root / "another.arp");
    changed(prepared.sceneTarget, fixture.files.root / "another.yaml");
    changed(prepared.root, fixture.files.root / "other");
    auto conflicts = std::move(prepared.conflicts);
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    prepared.conflicts = std::move(conflicts);
    const auto selected = prepared.document->selectedObject();
    const auto name = prepared.document->scene().findObject(selected)->name();
    assert(prepared.document->renameSelected("changed cold scene"));
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    assert(prepared.document->renameSelected(name));
    assert(Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    auto other = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic);
    assert(other);
    std::swap(candidate->recovery, other->recovery);
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-CANDIDATE-OWNER-CHANGED");
    std::swap(candidate->recovery, other->recovery);
    assert(Preparation::validateForWorkspace(*candidate, shell, diagnostic));
    const auto manager = prepared.resources.retainManager();
    const auto strongReferences = manager.use_count();
    assert(Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && manager.use_count() == strongReferences);
    core::pmr::String yaml;
    assert(EditorSceneAuthoringCodec::encode(*prepared.document, yaml, diagnostic));
    assert(EditorSceneAuthoringCodec::decode(yaml,
        *prepared.document,
        &EditorSceneAuthoringCodec::isResourceReady,
        &prepared.resources.manager(),
        diagnostic));
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-CANDIDATE-OWNER-CHANGED");
    candidate = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic);
    assert(candidate);
    candidate->recovery->document->setResourceManagerProvider([manager] {
        return manager.get();
    });
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-CANDIDATE-OWNER-CHANGED");
    Preparation::WorkspaceCandidate empty;
    assert(!Preparation::validateForWorkspace(empty, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-WORKSPACE-CANDIDATE-INCOMPLETE");
    assert(shell.stateVersion() == version
        && !std::filesystem::exists(fixture.files.root / "rules.arp"));
    std::puts(
        "PASS immutable admission binding, record/content/savepoint/path/conflict mutations, provisional IDs/history, exact-content owner replacement and no live/file mutation");
}

void candidateBudgetBinding(Fixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    auto snapshot = fixture.files.snapshot();
    EditorRecoveryBudget budget;
    budget.maxPayloadBytes = 4096;
    budget.maxScripts = 1;
    core::pmr::String diagnostic;
    auto candidate = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic, budget);
    assert(candidate);
    auto& prepared = *candidate->recovery;
    prepared.submitted.scripts[0].text.assign(8192, 'x');
    prepared.scripts[0].text = prepared.submitted.scripts[0].text;
    // The larger record is valid under defaults but not its original budget.
    assert(EditorRecoveryCodec::validate(prepared.submitted, diagnostic));
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-SCRIPT-LIMIT-OR-PATH");
    candidate = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic, budget);
    assert(candidate);
    candidate->recovery->scripts.push_back(candidate->recovery->scripts[0]);
    candidate->recovery->scripts[1].path = fixture.files.root / "another.arp";
    candidate->recovery->submitted.scripts.push_back(snapshot.scripts[0]);
    candidate->recovery->submitted.scripts[1].path = "another.arp";
    assert(EditorRecoveryCodec::validate(candidate->recovery->submitted, diagnostic));
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-CANDIDATE-STRUCTURE");
    std::puts(
        "PASS original reduced payload/script budgets cannot be enlarged by candidate changes");
}

void preparedWorkspaceState(Fixture& fixture)
{
    EditorShell shell;
    fixture.initialize(shell);
    const auto version = shell.stateVersion();
    auto snapshot = fixture.files.snapshot();
    snapshot.scene.savedFileBytes += "# exact saved spelling\r\n";
    core::pmr::String diagnostic;
    auto candidate = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic);
    assert(candidate && candidate->preparedState());
    const auto& state = *candidate->preparedState();
    assert(!state.projectDraft() && !state.projectDirty() && state.projectSavedBytes().empty()
        && state.scenePath() == snapshot.scene.path
        && state.sceneTarget() == fixture.files.root / "authoring.arp.yaml"
        && state.sceneSavedHash() == snapshot.scene.savedContentHash
        && std::string_view(reinterpret_cast<const char*>(state.sceneSavedBytes().data()),
               state.sceneSavedBytes().size())
            == snapshot.scene.savedFileBytes
        && state.activeScriptIndex() == 0 && state.workspace() == "Script");
    candidate->recovery->submitted.scene.savedFileBytes += "# forged baseline\n";
    assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
        && state.sceneSavedBytes().size() == snapshot.scene.savedFileBytes.size());
    snapshot.scene.path.clear();
    snapshot.scene.boundTarget.clear();
    snapshot.scene.savedContentHash.clear();
    snapshot.scene.savedFileBytes.clear();
    snapshot.scripts.clear();
    snapshot.activeScript.reset();
    snapshot.workspace = "3D";
    auto unbound = Preparation::prepareForWorkspace(snapshot, shell, false, diagnostic);
    assert(unbound && unbound->preparedState());
    const auto& empty = *unbound->preparedState();
    assert(empty.scenePath().empty() && empty.sceneTarget().empty()
        && empty.sceneSavedHash().empty() && empty.sceneSavedBytes().empty()
        && empty.activeScriptIndex() == ScriptDocumentSession::noActiveDocument
        && empty.workspace() == "3D" && shell.stateVersion() == version);
    std::puts(
        "PASS prepared bound/unbound savepoints preserve exact raw bytes, script selection and workspace without publishing live state");
}

void smoke(Fixture& fixture)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorShell shell;
            fixture.initialize(shell);
            core::pmr::String diagnostic;
            auto candidate = Preparation::prepareForWorkspace(
                fixture.files.snapshot(), shell, false, diagnostic);
            assert(candidate && Preparation::validateForWorkspace(*candidate, shell, diagnostic));
            candidate->recovery->scripts[0].text += "# candidate changed\n";
            assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic)
                && diagnostic == "EDITOR-RECOVERY-CANDIDATE-CONTENT-CHANGED");
            candidate = Preparation::prepareForWorkspace(
                fixture.files.snapshot(), shell, false, diagnostic);
            assert(candidate);
            assert(shell.openScriptFile(fixture.files.root / "first.arp"));
            assert(!Preparation::validateForWorkspace(*candidate, shell, diagnostic));
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary workspace admission/candidate/edit/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke(fixture);
    else {
        sceneFreshness(fixture);
        scriptFreshness(fixture);
        gatesAndBudgets(fixture);
        lifetimesAndCandidate(fixture);
        candidateContentBinding(fixture);
        candidateBudgetBinding(fixture);
        preparedWorkspaceState(fixture);
    }
}

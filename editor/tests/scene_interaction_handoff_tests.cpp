#include "audio/audio_components.h"
#include "core/text/number_format.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_recovery_admission.h"
#include "editor/workspace/editor_scene_transaction.h"

#include <cassert>
#include <cstdio>
#include <utility>

namespace {
using namespace arp;
using namespace arp::editor;
using Token = EditorShell::PreparedSceneReplacement;
using Result = EditorShell::SceneReplacementResult;

// Normal project publication must destroy old components before their borrowed
// manager. This witness deliberately holds only a weak reference.
class OwnerWitness final : public scene::Component {
public:
    OwnerWitness(std::weak_ptr<resource::ResourceManager> manager, bool& destroyed)
        : manager_(std::move(manager)), destroyed_(destroyed)
    {}

    ~OwnerWitness() override
    {
        assert(!manager_.expired());
        destroyed_ = true;
    }

private:
    std::weak_ptr<resource::ResourceManager> manager_;
    bool& destroyed_;
};

void execute(EditorShell& shell, EditorCommandPayload command)
{
    assert(shell.execute(command).success);
}

void initialize(EditorShell& shell, const tests::RecoveryFixture& files)
{
    assert(shell
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", files.root)
            .ok());
    execute(shell, NewSceneCommand{true});
    execute(shell, CreateSceneObjectCommand("Original"));
    execute(shell, AddSceneComponentCommand("AudioSourceComponent"));
    const auto objectId = shell.sceneDocument().selectedObject();
    const auto* source = shell.sceneDocument()
                             .scene()
                             .findObject(objectId)
                             ->getComponent<audio::AudioSourceComponent>();
    assert(source);
    SetSceneComponentPropertyCommand volume("volume");
    volume.objectId = objectId.value();
    volume.componentId = source->id().value();
    volume.numberValue = 0.35;
    execute(shell, volume);
    execute(shell, SetSceneObjectNameCommand("Edited"));
    execute(shell, SetSceneObjectTranslationCommand{2, 3, 4});
    execute(shell, UndoSceneCommand{});
}

void handoff(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    initialize(shell, files);
    const auto oldId = shell.sceneDocument().selectedObject();
    auto* oldObject = shell.sceneDocument().scene().findObject(oldId);
    const auto oldName = oldObject->name();
    const auto oldScene = &shell.sceneDocument().scene();
    const auto beforeVersion = shell.stateVersion();
    const auto beforeWorkspace = shell.workspace();
    const auto first = oldId.value();
    const auto second = shell.sceneDocument().createObject("Other").value();
    SelectSceneObjectsCommand selection;
    selection.objectIds = {first, second};
    selection.primaryObjectId = first;
    execute(shell, selection);
    core::pmr::String diagnostic;
    auto admission = EditorRecoveryAdmission::capture(shell, true, diagnostic);
    assert(admission);
    EditorSceneDocument candidate(false);
    candidate.scene().setName("Incoming Scene");
    const auto incoming = candidate.createObject("Incoming Selection");
    auto token = shell.prepareSceneReplacement(candidate, diagnostic);
    assert(token && diagnostic.empty() && !token->committed() && !token->retiredUndoCount()
        && &shell.sceneDocument().scene() == oldScene && oldObject->name() == oldName);
    const auto before = core::defaultCpuAllocator().stats();
    assert(shell.sceneReplacementReadiness(*token, candidate) == Result::Ready);
    assert(shell.commitSceneReplacement(*token, candidate) == Result::Committed);
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount && token->committed()
        && token->retiredUndoCount() >= 2 && token->retiredRedoCount() == 1
        && token->retiredSelectionCount() >= 1 && shell.stateVersion() == beforeVersion);
    assert(&candidate.scene() == oldScene && candidate.scene().findObject(oldId) == oldObject
        && oldObject->name() == oldName && shell.sceneDocument().selectedObject() == incoming
        && shell.selection() == "Incoming Selection" && shell.workspace() == beforeWorkspace);
    const auto state = shell.execute(GetEditorStateQuery{});
    assert(state.success && state.snapshot.scene == "Incoming Scene");
    assert(shell.commitSceneReplacement(*token, candidate) == Result::Consumed);
    assert(!EditorRecoveryAdmission::validate(shell, *admission, diagnostic));
    // The live history is empty; old objects/history remain in their original owners.
    const auto currentName = shell.sceneDocument().scene().findObject(incoming)->name();
    execute(shell, UndoSceneCommand{});
    execute(shell, RedoSceneCommand{});
    assert(shell.sceneDocument().scene().findObject(incoming)->name() == currentName
        && oldObject->name() == oldName && token->retiredRedoCount() == 1);
    const auto retiredUndo = token->retiredUndoCount();
    token.reset();
    assert(candidate.scene().findObject(oldId) == oldObject && oldObject->name() == oldName);
    std::printf(
        "PASS Scene/history/view/labels commit allocations=0, retired undo=%zu redo=1; history releases before original Scene\n",
        retiredUndo);
}

void freshnessAndFailure(const tests::RecoveryFixture& files)
{
    EditorShell shell, other;
    initialize(shell, files);
    EditorSceneDocument candidate(false);
    candidate.scene().setName("Candidate");
    const auto child = candidate.createObject("Selected");
    core::pmr::String diagnostic;
    Token incomplete;
    assert(shell.commitSceneReplacement(incomplete, candidate) == Result::Incomplete);
    auto token = shell.prepareSceneReplacement(candidate, diagnostic);
    assert(token && other.commitSceneReplacement(*token, candidate) == Result::Stale
        && shell.sceneReplacementReadiness(*token, candidate) == Result::Ready);
    const auto label = shell.selection();
    token.reset();
    assert(shell.selection() == label);
    execute(shell, RedoSceneCommand{});
    execute(shell, UndoSceneCommand{});
    unsigned rejectionCase = 0;
    const auto reject = [&](auto mutate) {
        auto prepared = shell.prepareSceneReplacement(candidate, diagnostic);
        assert(prepared);
        const auto* oldScene = &shell.sceneDocument().scene();
        const auto* coldScene = &candidate.scene();
        mutate();
        std::fprintf(stderr, "Scene handoff freshness case=%u\n", ++rejectionCase);
        assert(shell.commitSceneReplacement(*prepared, candidate) == Result::Stale
            && !prepared->committed() && &shell.sceneDocument().scene() == oldScene
            && &candidate.scene() == coldScene);
    };
    // execute() does not advance dispatcher version; the historical branch still changes.
    reject([&] {
        execute(shell, RedoSceneCommand{});
        execute(shell, UndoSceneCommand{});
    });
    reject([&] {
        candidate.scene().setName("Renamed Candidate");
    });
    reject([&] {
        candidate.scene().findObject(child)->setName("Renamed Selection");
    });
    reject([&] {
        shell.sceneDocument().setResourceManagerProvider(nullptr);
    });
    // Provider replacement invalidates the old Inspector observation. Reconcile
    // it before preparing the token that a real current collapse will stale.
    assert(shell.retrySceneInspector());
    reject([&] {
        const auto objectId = shell.sceneDocument().selectedObject();
        const auto* source = shell.sceneDocument()
                                 .scene()
                                 .findObject(objectId)
                                 ->getComponent<audio::AudioSourceComponent>();
        assert(source);
        core::pmr::String toggle("scene_component_toggle_");
        core::appendUnsigned(toggle, objectId.value());
        toggle.push_back('_');
        core::appendUnsigned(toggle, source->id().value());
        const auto version = shell.stateVersion();
        assert(shell.acceptsInspectorInput(toggle));
        shell.activate(toggle);
        assert(shell.stateVersion() == version);
    });
    reject([&] {
        const auto extra = shell.sceneDocument().createObject("extra");
        SelectSceneObjectsCommand selected;
        selected.objectIds = {shell.sceneDocument().scene().rootId().value(), extra.value()};
        selected.primaryObjectId = extra.value();
        execute(shell, selected);
    });
    token = shell.prepareSceneReplacement(candidate, diagnostic);
    assert(token);
    execute(shell, NewSceneCommand{true});
    assert(shell.commitSceneReplacement(*token, candidate) == Result::Stale
        && shell.selection() == "Untitled");
    std::puts(
        "PASS invalid, abandoned, cross-owner, exact-content history branch and candidate/view/provider freshness rejection");
}

void expired(const tests::RecoveryFixture& files)
{
    core::UniquePtr<Token> token;
    EditorSceneDocument candidate(false);
    {
        EditorShell original;
        initialize(original, files);
        core::pmr::String diagnostic;
        token = original.prepareSceneReplacement(candidate, diagnostic);
        assert(token);
    }
    EditorShell other;
    assert(other.commitSceneReplacement(*token, candidate) == Result::Stale);
    token.reset();
    core::pmr::String diagnostic;
    EditorShell live;
    initialize(live, files);
    EditorSceneDocument moved(std::move(candidate));
    assert(!live.prepareSceneReplacement(candidate, diagnostic));
    auto ready = live.prepareSceneReplacement(moved, diagnostic);
    assert(ready);
    EditorSceneDocument movedAgain(std::move(moved));
    assert(live.commitSceneReplacement(*ready, movedAgain) == Result::Stale);
    std::puts(
        "PASS expired Shell and moved native operands reject without dereferencing saved owners");
}

void quotaAndBudgets(const tests::RecoveryFixture& files)
{
    EditorShell shell;
    initialize(shell, files);
    EditorSceneDocument candidate(false);
    const core::pmr::String longName(EditorSceneDocument::maxObjectNameBytes, 'N');
    candidate.scene().setName(longName);
    candidate.createObject(longName);
    core::pmr::String diagnostic;
    diagnostic.reserve(128);
    const auto* oldScene = &shell.sceneDocument().scene();
    const auto* coldScene = &candidate.scene();
    const auto label = shell.selection();
    auto quota = core::makeShared<core::CpuMemoryQuota>();
    {
        core::CpuMemoryQuotaScope scope(quota);
        auto token = shell.prepareSceneReplacement(candidate, diagnostic);
        assert(token);
    }
    assert(quota->peakBytes() && !quota->liveBytes());
    quota->setLimitBytes(quota->peakBytes() - 1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!shell.prepareSceneReplacement(candidate, diagnostic)
            && diagnostic == "EDITOR-SCENE-HANDOFF-PREPARATION-FAILED");
    }
    assert(!quota->liveBytes() && &shell.sceneDocument().scene() == oldScene
        && &candidate.scene() == coldScene && shell.selection() == label);
    // Existing history remains usable after preparation failure.
    execute(shell, RedoSceneCommand{});
    execute(shell, UndoSceneCommand{});
    auto& nativeLabel = const_cast<core::pmr::String&>(shell.selection());
    nativeLabel.assign(Token::maxPreparedLabelBytes + 1, 'X');
    assert(!shell.prepareSceneReplacement(candidate, diagnostic)
        && diagnostic == "EDITOR-SCENE-HANDOFF-INVALID-OR-OVER-BUDGET");
    nativeLabel = label;
    candidate.scene().setName(core::pmr::String(EditorSceneDocument::maxObjectNameBytes + 1, 'X'));
    assert(!shell.prepareSceneReplacement(candidate, diagnostic)
        && diagnostic == "EDITOR-SCENE-HANDOFF-INVALID-LABEL");
    std::puts(
        "PASS exact name/display budgets and late quota failure clean partial state, preserving live Scene/history/labels");
}

void projectOwnerOrder(const tests::RecoveryFixture& files)
{
    bool destroyed = false;
    EditorShell shell;
    initialize(shell, files);
    std::weak_ptr<resource::ResourceManager> manager = shell.retainProjectResourceManager();
    auto* oldObject =
        shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    oldObject->addComponent<OwnerWitness>(manager, destroyed);
    execute(shell, CloseProjectCommand{true});
    assert(destroyed && manager.expired());
    const auto current = shell.sceneDocument().selectedObject();
    execute(shell, UndoSceneCommand{});
    execute(shell, RedoSceneCommand{});
    assert(shell.sceneDocument().scene().findObject(current));
    std::puts(
        "PASS actual project close destroys old components while their resource manager is alive, then releases the old owner and history");
}

void smoke(const tests::RecoveryFixture& files)
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        handoff(files);
        {
            EditorShell shell;
            initialize(shell, files);
            EditorSceneDocument candidate(false);
            core::pmr::String diagnostic;
            auto abandoned = shell.prepareSceneReplacement(candidate, diagnostic);
            assert(abandoned);
        }
        projectOwnerOrder(files);
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary Scene/history/view prepared/committed/abandoned destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    tests::RecoveryFixture files;
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        smoke(files);
    } else {
        handoff(files);
        freshnessAndFailure(files);
        expired(files);
        quotaAndBudgets(files);
        projectOwnerOrder(files);
    }
}

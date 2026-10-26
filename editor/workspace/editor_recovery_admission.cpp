#include "editor/workspace/editor_recovery_admission.h"

#include "core/io/filesystem_path.h"
#include "core/text/number_format.h"
#include "editor/workspace/editor_recovery_codec.h"
#include "editor/workspace/editor_recovery_digest.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {
namespace {
bool reject(core::pmr::String& diagnostic, const char* code)
{
    diagnostic = code;
    return false;
}

} // namespace

bool EditorRecoveryAdmission::gate(
    const EditorShell& shell, bool discardChanges, core::pmr::String& diagnostic)
{
    if (!shell.loaded_.ok() || shell.exitRequested_)
        return reject(diagnostic, "EDITOR-RECOVERY-LIVE-OWNER-INACTIVE");
    if (shell.authoringUiBlocked() || shell.activeTransformEditObjectId_ != 0
        || shell.sceneViewportGesture_ != EditorShell::SceneViewportGesture::None
        || shell.hierarchyDragging_ || shell.scriptDraggingSelection_
        || shell.scriptDraggingScrollbar_ || shell.fileDraggingScrollbar_
        || shell.sceneHierarchyDraggingScrollbar_)
        return reject(diagnostic, "EDITOR-RECOVERY-LIVE-INTERACTION-BUSY");
    const auto allowed = shell.projectReplacementGate(discardChanges);
    if (!allowed.success)
        return reject(diagnostic,
            allowed.diagnostic == EditorDiagnosticCode::UnsavedChanges
                ? "EDITOR-RECOVERY-LIVE-UNSAVED"
                : "EDITOR-RECOVERY-LIVE-WORKSPACE-BUSY");
    return true;
}

bool EditorRecoveryAdmission::fingerprint(const EditorShell& shell,
    EditorRecoveryAdmissionBudget budget,
    core::ByteBuffer& destination,
    core::pmr::String& diagnostic)
{
    if (shell.scriptDocuments_.size() > EditorRecoveryBudget{}.maxScripts)
        return reject(diagnostic, "EDITOR-RECOVERY-LIVE-SCRIPT-COUNT");
    detail::RecoveryDigest digest(budget.maxObservedBytes);
    digest.field("arp.editor-recovery-live-observation/1");
    digest.field(core::FilePath::utf8(shell.projectRoot()));
    digest.number(shell.projectManifest_.has_value());
    if (shell.projectManifest_) {
        core::pmr::String draft;
        ProjectDiagnostic issue;
        if (!ProjectManifestCodec::encode(*shell.projectManifest_, draft, issue))
            return reject(diagnostic, "EDITOR-RECOVERY-LIVE-PROJECT-INVALID");
        digest.field(draft);
    }
    digest.number(shell.projectManifestDirty_);
    digest.field(shell.projectResources_.manifestSnapshot());
    digest.field(shell.projectResources_.metadataSnapshot());
    digest.field(shell.projectResources_.packageProvenanceDiagnostic());
    const auto& inputs = shell.projectResources_.inputBindings();
    digest.number(inputs.size());
    for (const auto& input : inputs) {
        digest.field(input.path);
        digest.number(input.byteCount);
        digest.field(input.fingerprint);
    }
    const auto& packages = shell.projectResources_.packageBindings();
    digest.number(packages.size());
    for (const auto& package : packages) {
        digest.field(package.directory);
        digest.field(package.fingerprint);
    }
    core::pmr::String sceneYaml;
    if (!EditorSceneAuthoringCodec::encode(shell.sceneDocument_, sceneYaml, diagnostic))
        return false;
    digest.field(sceneYaml);
    digest.field(shell.savedSceneHash_);
    digest.field(shell.activeScenePath_);
    digest.field(core::FilePath::utf8(shell.savedSceneFileTarget_));
    const auto& savedBytes = shell.savedSceneFileBytes_;
    digest.field(savedBytes.empty()
            ? std::string_view{}
            : std::string_view(
                  reinterpret_cast<const char*>(savedBytes.data()), savedBytes.size()));
    const auto hierarchy = shell.sceneDocument_.hierarchyEntries();
    digest.number(hierarchy.size());
    for (const auto& entry : hierarchy) {
        const auto* object = shell.sceneDocument_.scene().findObject(entry.objectId);
        if (!object)
            return reject(diagnostic, "EDITOR-RECOVERY-LIVE-SCENE-IDENTITY");
        digest.number(entry.objectId.value());
        digest.number(entry.depth);
        digest.number(object->active());
        const auto transform = object->transform().localTransform();
        for (const auto value : {transform.translation.x,
                 transform.translation.y,
                 transform.translation.z,
                 transform.rotation.x,
                 transform.rotation.y,
                 transform.rotation.z,
                 transform.rotation.w,
                 transform.scale.x,
                 transform.scale.y,
                 transform.scale.z}) {
            core::pmr::String number;
            if (!core::appendFloating(number, value))
                return reject(diagnostic, "EDITOR-RECOVERY-LIVE-SCENE-TRANSFORM");
            digest.field(number);
        }
        digest.number(object->components().size());
        for (const auto& component : object->components()) {
            digest.number(component->id().value());
            digest.number(component->enabled());
        }
    }
    digest.number(shell.sceneDocument_.selectedObject().value());
    digest.number(shell.sceneViewportAdditionalSelection_.size());
    for (const auto objectId : shell.sceneViewportAdditionalSelection_)
        digest.number(objectId.value());
    digest.field(shell.workspace());
    digest.number(shell.scriptDocuments_.activeIndex());
    digest.number(shell.scriptDocuments_.size());
    for (const auto& document : shell.scriptDocuments_.documents()) {
        if (document.undo.size() > budget.maxScriptHistoryEntries
            || document.redo.size() > budget.maxScriptHistoryEntries - document.undo.size())
            return reject(diagnostic, "EDITOR-RECOVERY-LIVE-HISTORY-BUDGET");
        digest.number(document.id);
        digest.field(core::FilePath::utf8(document.path));
        digest.field(document.text);
        digest.field(document.savedText);
        digest.field(document.savedFileBytes);
        digest.field(document.newline);
        digest.number(document.cursor);
        digest.number(document.anchor);
        digest.number(document.scrollLine);
        const auto history = [&digest](const auto& edits) {
            digest.number(edits.size());
            for (const auto& edit : edits) {
                digest.field(edit.removed);
                digest.field(edit.inserted);
                digest.number(edit.offset);
                digest.number(edit.cursorBefore);
                digest.number(edit.anchorBefore);
                digest.number(edit.cursorAfter);
                digest.number(edit.anchorAfter);
            }
        };
        history(document.undo);
        history(document.redo);
    }
    if (!digest.valid())
        return reject(diagnostic, "EDITOR-RECOVERY-LIVE-BYTE-BUDGET");
    destination = digest.finish();
    diagnostic.clear();
    return true;
}

core::UniquePtr<EditorRecoveryAdmission::Ticket> EditorRecoveryAdmission::capture(
    const EditorShell& shell,
    bool discardChanges,
    core::pmr::String& diagnostic,
    EditorRecoveryAdmissionBudget budget)
{
    try {
        const EditorRecoveryAdmissionBudget maximum;
        if (budget.maxObservedBytes > maximum.maxObservedBytes
            || budget.maxScriptHistoryEntries > maximum.maxScriptHistoryEntries) {
            reject(diagnostic, "EDITOR-RECOVERY-LIVE-BUDGET-INVALID");
            return {};
        }
        if (!gate(shell, discardChanges, diagnostic))
            return {};
        auto ticket = core::makeUnique<Ticket>();
        ticket->shell_ = shell.recoveryOwnerEpoch_.ticket();
        ticket->scene_ = shell.sceneDocument_.ownerTicket();
        ticket->scripts_ = shell.scriptDocuments_.stateTicket();
        ticket->history_ = shell.sceneHistoryEpoch_.ticket();
        if (!ticket->shell_.isCurrent() || !ticket->scene_.isCurrent()
            || !ticket->scripts_.isCurrent() || !ticket->history_.isCurrent()) {
            reject(diagnostic, "EDITOR-RECOVERY-LIVE-EPOCH-EXHAUSTED");
            return {};
        }
        ticket->resources_ = shell.projectResources_.retainManager();
        const auto physics = shell.projectResources_.physicsWorld2D();
        ticket->physics_ = physics;
        ticket->hasPhysics_ = bool(physics);
        ticket->discardChanges_ = discardChanges;
        ticket->stateVersion_ = shell.stateVersion();
        ticket->documentEpoch_ = shell.sceneDocumentEpoch_;
        ticket->budget_ = budget;
        if (!fingerprint(shell, budget, ticket->fingerprint_, diagnostic))
            return {};
        return ticket;
    } catch (...) {
        reject(diagnostic, "EDITOR-RECOVERY-LIVE-OBSERVATION-FAILED");
        return {};
    }
}

bool EditorRecoveryAdmission::validate(
    const EditorShell& shell, const Ticket& ticket, core::pmr::String& diagnostic)
{
    try {
        if (!ticket.shell_.sameCurrentEpoch(shell.recoveryOwnerEpoch_.ticket())
            || !ticket.scene_.sameCurrentEpoch(shell.sceneDocument_.ownerTicket())
            || !ticket.scripts_.sameCurrentEpoch(shell.scriptDocuments_.stateTicket())
            || !ticket.history_.sameCurrentEpoch(shell.sceneHistoryEpoch_.ticket())
            || ticket.stateVersion_ != shell.stateVersion()
            || ticket.documentEpoch_ != shell.sceneDocumentEpoch_)
            return reject(diagnostic, "EDITOR-RECOVERY-LIVE-STALE");
        const auto resources = ticket.resources_.lock();
        const auto physics = ticket.physics_.lock();
        if (!resources || resources != shell.projectResources_.retainManager()
            || (ticket.hasPhysics_ && !physics)
            || physics != shell.projectResources_.physicsWorld2D())
            return reject(diagnostic, "EDITOR-RECOVERY-LIVE-OWNER-CHANGED");
        if (!gate(shell, ticket.discardChanges_, diagnostic))
            return false;
        core::ByteBuffer current;
        if (!fingerprint(shell, ticket.budget_, current, diagnostic))
            return false;
        if (current != ticket.fingerprint_)
            return reject(diagnostic, "EDITOR-RECOVERY-LIVE-CONTENT-CHANGED");
        diagnostic.clear();
        return true;
    } catch (...) {
        return reject(diagnostic, "EDITOR-RECOVERY-LIVE-OBSERVATION-FAILED");
    }
}
} // namespace arp::editor

#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>

namespace arp::editor {

bool EditorShell::sourceScenePending() const noexcept
{
    return std::any_of(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [](const auto& task) {
            return task.status == ScenePersistenceTaskStatus::Pending
                && (task.sourceTask || task.sourceSave);
        });
}

EditorCommandOutcome EditorShell::cancelSourceScenePersistence(std::uint64_t id)
{
    auto found = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [id](const auto& task) {
            return id && task.id == id;
        });
    if (found == scenePersistenceTasks_.end()
        || found->status != ScenePersistenceTaskStatus::Pending)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (found->sourceSave) {
        found->sourceSave->cancel();
        found->saveReceipt.cancellationRequested = true;
        lastSceneSave_ = found->saveReceipt;
        // Even cancellation retains a submitted operation until its real result.
        return completeSourceSceneSave(id);
    }
    if (!found->sourceTask)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    found->sourceTask->cancel();
    found->status = ScenePersistenceTaskStatus::Cancelled;
    found->diagnostic = "EDITOR-SCENE-SOURCE-CANCELLED";
    found->sourceTask.reset();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::completeSourceScenePersistence(std::uint64_t id)
{
    auto found = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [id](const auto& task) {
            return id && task.id == id;
        });
    if (found == scenePersistenceTasks_.end() || !found->sourceTask
        || found->status != ScenePersistenceTaskStatus::Pending)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    auto& task = *found;
    const auto fail = [&](std::string_view issue) -> EditorCommandOutcome {
        if (issue.size() > projectIoBudget_.maxDiagnosticBytes)
            issue = "EDITOR-SCENE-SOURCE-DIAGNOSTIC-BUDGET";
        task.diagnostic.assign(issue.data(), issue.size());
        task.status = ScenePersistenceTaskStatus::Failed;
        task.sourceTask.reset();
        return {true, true, EditorDiagnosticCode::None};
    };
    try {
        if (task.documentEpoch != sceneDocumentEpoch_
            || !task.owner.sameCurrentEpoch(sceneDocument_.ownerTicket())
            || !task.scripts.sameCurrentEpoch(scriptDocuments_.stateTicket())
            || task.resourceRevision != resourceRevision_ || task.projectRoot != projectRoot())
            return fail("EDITOR-SCENE-SOURCE-OWNER-CHANGED");
        task.sourceTask->advance();
        if (task.sourceTask->state() == SceneSourceState::Pending)
            return {true, false, EditorDiagnosticCode::None};
        if (task.sourceTask->state() != SceneSourceState::Ready)
            return fail(task.sourceTask->diagnostic());
        core::pmr::String hash, diagnostic, model;
        ProjectDiagnostic issue;
        if (!projectManifest_ || !ProjectManifestCodec::encode(*projectManifest_, model, issue)
            || model != task.projectModel
            || projectResources_.manifestSnapshot() != task.manifestBytes
            || !observeSceneHash(hash, diagnostic) || hash != task.contentHash)
            return fail("EDITOR-SCENE-SOURCE-CONTEXT-CHANGED");
        const auto& inputSource = task.sourceTask->context();
        const auto& directory = projectBrowser_.sourceSnapshot();
        if (!directory || directory->project() != inputSource.snapshot)
            return fail("EDITOR-SCENE-SOURCE-PROJECT-CHANGED");
        const auto yaml = task.sourceTask->text();
        const auto version = EditorSceneFileFormat::version(yaml);
        core::UniquePtr<EditorSceneDocument> candidate;
        if (version == EditorSceneScriptCodec::contractVersion) {
            auto input = EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_);
            input.sources = &inputSource;
            if (!EditorSceneScriptCodec::prepare(yaml, sceneDocument_, input, candidate, diagnostic)
                || !EditorSceneScriptCodec::observeContentHash(*candidate, input, hash, diagnostic))
                return fail(diagnostic);
        } else {
            candidate = core::makeUnique<EditorSceneDocument>(false);
            candidate->copySceneServicesFrom(sceneDocument_);
            const auto provider = sceneDocument_.resourceManagerProvider();
            if (!EditorSceneAuthoringCodec::decode(yaml,
                    *candidate,
                    &EditorSceneAuthoringCodec::isResourceReady,
                    provider ? provider() : nullptr,
                    diagnostic)
                || !EditorSceneAuthoringCodec::contentHash(*candidate, hash, diagnostic))
                return fail(diagnostic);
        }
        // Save-point ownership and handoff are prepared before commit. This raw
        // baseline remains separate from the Text/1 input (including BOM/CRLF).
        core::pmr::Vector<std::uint8_t> bytes(
            task.sourceTask->scene().receipt.bytes, savedSceneFileBytes_.get_allocator());
        auto replacement = prepareSceneReplacement(*candidate, diagnostic);
        const char* sourceIssue = nullptr;
        if (!replacement || !inputSource.validate(sourceIssue))
            return fail(sourceIssue ? std::string_view(sourceIssue) : std::string_view(diagnostic));
        if (commitSceneReplacement(*replacement, *candidate) != SceneReplacementResult::Committed)
            return fail("EDITOR-SCENE-SOURCE-REPLACEMENT-STALE");
        sceneAuthoringVersion_ = version;
        savedSceneHash_.swap(hash);
        activeScenePath_.swap(task.relativePath);
        savedSceneFileTarget_.clear();
        savedSceneFileBytes_.swap(bytes);
        savedSceneSourceScope_ = inputSource.snapshot;
        sourceSceneSaveUnknown_ = false;
        if (lastSceneSave_.taskId)
            lastSceneSave_.baselineRefreshed = true;
        ++sceneDocumentEpoch_;
        task.version = version;
        task.status = ScenePersistenceTaskStatus::Succeeded;
        task.sourceTask.reset();
    } catch (const core::YamlContractError& error) {
        return fail(error.diagnostic);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    try {
        refreshSceneInspector();
    } catch (...) {
        // Publication is terminal truth even if UI preparation is refused.
        failSceneInspector("EDITOR-SCENE-SOURCE-PRESENTATION");
        task.diagnostic = "EDITOR-SCENE-SOURCE-PRESENTATION";
    }
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::beginSourceSceneSave(std::string_view relative, bool overwrite)
try {
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    const auto& directory = projectBrowser_.sourceSnapshot();
    if (!directory || !projectManifest_ || !projectSourceFiles_ || !nextScenePersistenceTaskId_
        || !ProjectInputSnapshot::validLogicalPath(relative) || sourceSceneSaveUnknown_)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    const auto& scope = directory->project();
    if (!projectSourceFiles_->saveCapability(scope->location().mount).observedBaselineReplace)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    const bool bound = relative == activeScenePath_ && savedSceneSourceScope_ == scope;
    if (relative == activeScenePath_ && !bound)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    auto available = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [](const auto& task) {
            return !task.id;
        });
    if (available == scenePersistenceTasks_.end())
        available = std::min_element(scenePersistenceTasks_.begin(),
            scenePersistenceTasks_.end(),
            [](const auto& a, const auto& b) {
                return a.id < b.id;
            });
    ScenePersistenceTaskRecord prepared;
    prepared.id = nextScenePersistenceTaskId_;
    prepared.diagnostic.reserve(projectIoBudget_.maxDiagnosticBytes);
    prepared.relativePath = relative;
    prepared.version = sceneAuthoringVersion_;
    prepared.documentEpoch = sceneDocumentEpoch_;
    prepared.owner = sceneDocument_.ownerTicket();
    prepared.resourceRevision = resourceRevision_;
    prepared.projectRoot = projectRoot();
    core::pmr::String diagnostic;
    ProjectDiagnostic issue;
    if (!observeSceneHash(prepared.contentHash, diagnostic)
        || !ProjectManifestCodec::encode(*projectManifest_, prepared.projectModel, issue))
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    prepared.manifestBytes = projectResources_.manifestSnapshot();
    prepared.sourceSave = core::makeUnique<EditorSceneSourceSaveTask>(
        ProjectSourceContext{scope, projectSourceFiles_, scope->location().epoch},
        relative,
        projectIoBudget_.inputs,
        EditorSceneAuthoringCodec::maxDocumentBytes,
        bound,
        bound ? savedSceneFileBytes_ : core::pmr::Vector<std::uint8_t>{},
        overwrite);
    if (prepared.sourceSave->terminal()) {
        feedback(prepared.sourceSave->diagnostic());
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    prepared.status = ScenePersistenceTaskStatus::Pending;
    prepared.saveReceipt.taskId = prepared.id;
    prepared.saveReceipt.documentId = prepared.documentEpoch;
    EditorCommandOutcome result;
    result.stateChanged = false;
    result.hasSnapshot = true;
    appendProjectSnapshot(result.snapshot);
    result.snapshot.sceneIoPending = true;
    result.snapshot.scenePersistenceTaskId = prepared.id;
    result.snapshot.scenePersistenceTaskStatus = ScenePersistenceTaskStatus::Pending;
    result.snapshot.sceneSaveReceipt = prepared.saveReceipt;
    *available = std::move(prepared);
    lastSceneSave_ = available->saveReceipt;
    ++nextScenePersistenceTaskId_;
    return result;
} catch (...) {
    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
}

EditorCommandOutcome EditorShell::completeSourceSceneSave(std::uint64_t id)
{
    auto found = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [id](const auto& task) {
            return id && task.id == id;
        });
    if (found == scenePersistenceTasks_.end() || !found->sourceSave
        || found->status != ScenePersistenceTaskStatus::Pending)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    auto& task = *found;
    auto& save = *task.sourceSave;
    const auto receipt = [&] {
        task.saveReceipt.submitted = save.submitted();
        task.saveReceipt.terminal = save.terminal();
        task.saveReceipt.cancellationRequested = save.cancelled();
        task.saveReceipt.outcome = save.result();
        task.saveReceipt.maintenance = save.receiptObservation();
        task.committed = save.result().commitState == core::io::FileCommitState::Committed;
        lastSceneSave_ = task.saveReceipt;
    };
    const auto finish = [&](const char* issue) -> EditorCommandOutcome {
        receipt();
        task.status = issue ? (save.cancelled() ? ScenePersistenceTaskStatus::Cancelled
                                                : ScenePersistenceTaskStatus::Failed)
                            : ScenePersistenceTaskStatus::Succeeded;
        if (issue && issue != task.diagnostic.c_str())
            task.diagnostic = issue;
        task.sourceSave.reset();
        core::pmr::Vector<std::uint8_t>{}.swap(task.sourceSavedBytes);
        core::pmr::String{}.swap(task.saveYaml);
        // Terminal IO truth precedes presentation; no failure here replays IO.
        try {
            if (issue) {
                feedback(detail::sceneFileFailureMessage(task.diagnostic));
                presentationState_.openOutput();
                visible("output_panel", true);
            } else
                feedback("Saved scene source.");
        } catch (...) {
        }
        return {true, true, EditorDiagnosticCode::None};
    };
    const auto rejectPreparation = [&](const char* issue) -> EditorCommandOutcome {
        save.fail(issue);
        receipt();
        if (!save.terminal())
            return {true, true, EditorDiagnosticCode::None};
        return finish(issue);
    };
    try {
        const auto& directory = projectBrowser_.sourceSnapshot();
        const char* sourceIssue = nullptr;
        core::pmr::String diagnostic, model, hash;
        ProjectDiagnostic problem;
        const bool current = task.documentEpoch == sceneDocumentEpoch_
            && task.owner.sameCurrentEpoch(sceneDocument_.ownerTicket())
            && task.resourceRevision == resourceRevision_ && directory
            && directory->project() == save.context().snapshot
            && save.context().validate(sourceIssue) && projectManifest_
            && ProjectManifestCodec::encode(*projectManifest_, model, problem)
            && model == task.projectModel
            && projectResources_.manifestSnapshot() == task.manifestBytes;
        if (!current)
            save.cancel();
        save.advance();
        receipt();
        if (save.terminal()) {
            const auto& outcome = save.result();
            if (outcome.commitState == core::io::FileCommitState::OutcomeUnknown) {
                sourceSceneSaveUnknown_ = true;
                return finish("EDITOR-SCENE-SOURCE-SAVE-OUTCOME-UNKNOWN");
            }
            if (!current || save.cancelled())
                return finish(task.committed ? "EDITOR-SCENE-SOURCE-CANCELLED-AFTER-COMMIT"
                                             : "EDITOR-SCENE-SOURCE-SAVE-CANCELLED");
            if (outcome.status != core::io::FileIoStatus::Success
                || outcome.deliveryStatus != core::io::FileIoStatus::Success || !task.committed)
                return finish(outcome.status == core::io::FileIoStatus::VersionMismatch
                        ? "EDITOR-SCENE-FILE-EXTERNAL-CHANGE"
                        : (outcome.diagnosticId ? outcome.diagnosticId
                                                : "EDITOR-SCENE-SOURCE-SAVE-NOT-COMMITTED"));
            if (outcome.committedVersion != save.intendedVersion())
                return finish("EDITOR-SCENE-SOURCE-SAVE-RECEIPT-MISMATCH");
            savedSceneHash_.swap(task.contentHash);
            activeScenePath_.swap(task.relativePath);
            savedSceneFileBytes_.swap(task.sourceSavedBytes);
            savedSceneSourceScope_ = save.context().snapshot;
            savedSceneFileTarget_.clear();
            task.saveReceipt.published = true;
            return finish(nullptr);
        }
        if (!save.ready())
            return {true, true, EditorDiagnosticCode::None};
        if (!observeSceneHash(hash, diagnostic) || hash != task.contentHash) {
            save.fail("EDITOR-SCENE-SOURCE-CONTENT-CHANGED");
            return finish(save.diagnostic());
        }
        const auto& context = save.context();
        const auto& previous = save.previousBytes();
        const auto prior = previous.empty()
            ? std::string_view{}
            : std::string_view(reinterpret_cast<const char*>(previous.data()), previous.size());
        if (task.version == EditorSceneScriptCodec::contractVersion) {
            auto input = EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_);
            input.sources = &context;
            core::UniquePtr<EditorSceneScriptFilePreparation::Prepared> token;
            if (!EditorSceneScriptFilePreparation::prepare(sceneDocument_,
                    input,
                    {task.relativePath, save.targetExisted(), prior},
                    token,
                    diagnostic,
                    scriptReloadProof_.get())) {
                save.fail("EDITOR-SCENE-SOURCE-V4-PREFLIGHT-REJECTED");
                task.diagnostic = diagnostic;
                return finish(task.diagnostic.c_str());
            }
            task.sourceSavedBytes = token->replacementBytes();
        } else if (task.version == EditorSceneAuthoringCodec::contractVersion) {
            if (save.targetExisted()) {
                EditorSceneDocument previousDocument(false);
                previousDocument.copySceneServicesFrom(sceneDocument_);
                const auto provider = sceneDocument_.resourceManagerProvider();
                if (EditorSceneFileFormat::version(prior) != task.version
                    || !EditorSceneAuthoringCodec::decode(prior,
                        previousDocument,
                        &EditorSceneAuthoringCodec::isResourceReady,
                        provider ? provider() : nullptr,
                        diagnostic)) {
                    save.fail("EDITOR-SCENE-SOURCE-V3-PREFLIGHT-REJECTED");
                    return finish(save.diagnostic());
                }
            }
            if (!EditorSceneAuthoringCodec::encode(sceneDocument_, task.saveYaml, diagnostic)) {
                save.fail("EDITOR-SCENE-SOURCE-ENCODE-REJECTED");
                return finish(save.diagnostic());
            }
            EditorSceneDocument verified(false);
            verified.copySceneServicesFrom(sceneDocument_);
            const auto provider = sceneDocument_.resourceManagerProvider();
            if (!EditorSceneAuthoringCodec::decode(task.saveYaml,
                    verified,
                    &EditorSceneAuthoringCodec::isResourceReady,
                    provider ? provider() : nullptr,
                    diagnostic)) {
                save.fail("EDITOR-SCENE-SOURCE-COLD-ADMISSION-REJECTED");
                return finish(save.diagnostic());
            }
            task.sourceSavedBytes.assign(task.saveYaml.begin(), task.saveYaml.end());
        } else {
            save.fail("EDITOR-SCENE-FILE-VERSION");
            return finish(save.diagnostic());
        }
        if (!save.write(task.sourceSavedBytes))
            return finish(save.diagnostic());
        return {true, true, EditorDiagnosticCode::None};
    } catch (const core::YamlContractError& error) {
        return rejectPreparation(error.diagnostic);
    } catch (const std::exception&) {
        return rejectPreparation("EDITOR-SCENE-SOURCE-SAVE-PREPARATION");
    }
}
} // namespace arp::editor

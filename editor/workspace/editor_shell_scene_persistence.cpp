#include "core/io/file_io.h"
#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_script_policy.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>
#include <system_error>

namespace arp::editor {
namespace {
std::string_view byteText(const core::pmr::Vector<std::uint8_t>& bytes) noexcept
{
    return bytes.empty()
        ? std::string_view{}
        : std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace

bool EditorShell::sceneFileContextCurrent(const std::filesystem::path& root,
    std::string_view model,
    std::string_view manifestBytes,
    core::pmr::String& diagnostic) const
{
    if (!projectManifest_ || root != projectRoot() || projectSaveRequest_) {
        diagnostic = "EDITOR-SCENE-FILE-PROJECT-STALE";
        return false;
    }
    ProjectDiagnostic issue;
    core::pmr::String current;
    if (!ProjectManifestCodec::encode(*projectManifest_, current, issue) || current != model
        || manifestBytes != projectResources_.manifestSnapshot()) {
        diagnostic = "EDITOR-SCENE-FILE-PROJECT-STALE";
        return false;
    }
    std::filesystem::path path;
    if (!ProjectManifestCodec::resolvePath(
            root, "project.arp.yaml", ProjectPathIntent::ExistingFile, path, issue)) {
        diagnostic = "EDITOR-SCENE-FILE-MANIFEST-UNAVAILABLE";
        return false;
    }
    const auto disk = core::readTextFile(path, EditorProjectResources::maxManifestBytes);
    if (!disk.success || disk.text != manifestBytes) {
        diagnostic = "EDITOR-SCENE-FILE-MANIFEST-CHANGED";
        return false;
    }
    if (!ProjectInputSnapshot::verify(root, projectResources_.inputBindings(), diagnostic))
        return false;
    const auto catalog = sceneDocument_.scriptCatalog();
    if (catalog && !catalog->modules().verifySources(root, diagnostic))
        return false;
    return true;
}

EditorCommandOutcome EditorShell::scenePersistenceTaskSnapshot(std::uint64_t taskId) const
{
    const auto found = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [taskId](const auto& task) {
            return task.id == taskId;
        });
    if (!taskId || found == scenePersistenceTasks_.end())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    EditorCommandOutcome result;
    result.stateChanged = false;
    result.hasSnapshot = true;
    appendProjectSnapshot(result.snapshot);
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    result.snapshot.scenePersistenceTaskId = found->id;
    result.snapshot.scenePersistenceTaskStatus = found->status;
    result.snapshot.scenePersistenceTaskDiagnostic = found->diagnostic;
    result.snapshot.scenePersistenceTaskCommitted = found->committed;
    return result;
}

EditorCommandOutcome EditorShell::beginScenePersistence(
    bool loading, std::string_view relativePath, bool discardChanges, bool overwriteExisting)
try {
    if (sceneExport_.pending())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (loading) {
        const auto gate = sceneReplacementGate(discardChanges);
        if (!gate.success)
            return gate;
    }
    if (std::any_of(
            scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [](const auto& task) {
                return task.status == ScenePersistenceTaskStatus::Pending;
            }))
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
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
    // Prepare every owned receipt before IO can commit. Failed preparation never
    // changes an old task, the document, or the clean baseline.
    ScenePersistenceTaskRecord prepared;
    prepared.diagnostic.reserve(256);
    prepared.relativePath = relativePath;
    prepared.version = sceneAuthoringVersion_;
    prepared.loading = loading;
    prepared.documentEpoch = sceneDocumentEpoch_;
    prepared.owner = sceneDocument_.ownerTicket();
    prepared.projectRoot = projectRoot();
    core::pmr::String diagnostic;
    if (!observeSceneHash(prepared.contentHash, diagnostic)) {
        feedback(diagnostic);
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    if (projectManifest_) {
        ProjectDiagnostic issue;
        if (!ProjectManifestCodec::encode(*projectManifest_, prepared.projectModel, issue))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        prepared.manifestBytes = projectResources_.manifestSnapshot();
    }
    EditorCommandOutcome result;
    result.stateChanged = false;
    result.hasSnapshot = true;
    appendProjectSnapshot(result.snapshot);
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    if (loading) {
        // Raw bytes are not admission. The owner dispatches the explicit wire
        // version and prepares a complete candidate before changing live state.
        prepared.request =
            scenePersistence_.readScript(projectRoot(), relativePath, &prepared.resolvedTarget);
    } else {
        const char* issue = nullptr;
        if (!EditorScenePersistence::resolvePath(
                projectRoot(), relativePath, false, prepared.resolvedTarget, issue)) {
            feedback(issue ? issue : "EDITOR-SCENE-PATH-INVALID");
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        }
        std::error_code error;
        const bool exists = std::filesystem::exists(prepared.resolvedTarget, error);
        if (error)
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        bool bound =
            !savedSceneFileTarget_.empty() && prepared.resolvedTarget == savedSceneFileTarget_;
        if (!bound && exists && !savedSceneFileTarget_.empty()) {
            const bool previousExists = std::filesystem::exists(savedSceneFileTarget_, error);
            if (!error && previousExists)
                bound = std::filesystem::equivalent(
                    savedSceneFileTarget_, prepared.resolvedTarget, error);
            if (error) {
                feedback("EDITOR-SCENE-FILE-IDENTITY-UNAVAILABLE");
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            }
        }
        if (relativePath == activeScenePath_ && !bound) {
            feedback("EDITOR-SCENE-FILE-TARGET-CHANGED: reload or save to a different path.");
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        }
        if (exists && !bound && !overwriteExisting)
            return {false, false, EditorDiagnosticCode::SceneOverwriteRequired};
        if (prepared.version == EditorSceneScriptCodec::contractVersion) {
            if (!sceneFileContextCurrent(prepared.projectRoot,
                    prepared.projectModel,
                    prepared.manifestBytes,
                    diagnostic)) {
                feedback(diagnostic);
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            }
            const auto input = EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_);
            if (exists && !bound) {
                // An explicit overwrite still needs the previous v4 file's cold
                // validation. Changes during this read reject the preflight.
                if (!EditorSceneScriptCodec::encode(
                        sceneDocument_, input, prepared.saveYaml, diagnostic)) {
                    feedback(diagnostic);
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                prepared.checkingFile = true;
                prepared.request = scenePersistence_.readResolved(prepared.resolvedTarget);
            } else {
                core::UniquePtr<EditorSceneScriptFilePreparation::Prepared> token;
                if (!EditorSceneScriptFilePreparation::prepare(sceneDocument_,
                        input,
                        {relativePath,
                            bound,
                            bound ? byteText(savedSceneFileBytes_) : std::string_view{}},
                        token,
                        diagnostic)) {
                    feedback(diagnostic);
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
                prepared.request =
                    scenePersistence_.saveScript(sceneDocument_, input, *token, diagnostic);
            }
        } else if (prepared.version == EditorSceneAuthoringCodec::contractVersion) {
            // The native v3 encoder/store gates remain unchanged. No failure
            // can implicitly upgrade this document to a different wire format.
            for (const auto& object : sceneDocument_.scene().objects())
                for (const auto& property : sceneDocument_.inspectorProperties(object->id())) {
                    if (property.kind != SceneInspectorPropertyKind::ResourceReference
                        || property.stringValue.empty())
                        continue;
                    if (property.resourceType.empty()
                        || !EditorSceneAuthoringCodec::isResourceReady(
                            detail::sceneResourceManager(sceneDocument_),
                            property.resourceType,
                            property.stringValue))
                        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                }
            if (!EditorSceneAuthoringCodec::encode(sceneDocument_, prepared.saveYaml, diagnostic))
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
            prepared.checkingFile = bound;
            prepared.request = bound
                ? scenePersistence_.readResolved(prepared.resolvedTarget)
                : scenePersistence_.saveResolved(prepared.resolvedTarget, prepared.saveYaml);
            if (!bound)
                prepared.saveYaml.clear();
        } else
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    if (!prepared.request) {
        if (diagnostic.empty() && scenePersistence_.diagnostic())
            diagnostic = scenePersistence_.diagnostic();
        feedback(diagnostic.empty() ? "EDITOR-SCENE-FILE-SUBMIT" : diagnostic);
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    prepared.id = nextScenePersistenceTaskId_++;
    prepared.status = ScenePersistenceTaskStatus::Pending;
    *available = std::move(prepared);
    result.snapshot.sceneIoPending = true;
    result.snapshot.scenePersistenceTaskId = available->id;
    result.snapshot.scenePersistenceTaskStatus = available->status;
    return result;
} catch (const std::bad_alloc&) {
    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
}

void EditorShell::pollScenePersistence()
{
    for (auto& task : scenePersistenceTasks_) {
        if (task.status != ScenePersistenceTaskStatus::Pending)
            continue;
        // An internal command rejected before execution must remain retryable;
        // never consume an already taken result a second time.
        if (!task.result) {
            core::io::AtomicFileResult result;
            if (!scenePersistence_.take(task.request, result))
                continue;
            task.request = {};
            task.result = std::move(result);
        }
        try {
            (void)submitCommand(
                CompleteScenePersistenceCommand{task.id}, EditorCommandOrigin::Internal);
        } catch (const std::bad_alloc&) {
            // Before execution the taken receipt stays pending for a later
            // owner-thread retry. Executed commands publish terminal IO truth
            // before fallible presentation; never replay an accepted write.
        }
    }
}

EditorCommandOutcome EditorShell::completeScenePersistence(std::uint64_t taskId)
{
    const auto found = std::find_if(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [taskId](const auto& task) {
            return task.id == taskId;
        });
    if (found == scenePersistenceTasks_.end() || !found->result
        || found->status != ScenePersistenceTaskStatus::Pending)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    auto& task = *found;
    auto file = std::move(*task.result);
    task.result.reset();
    task.committed = file.committed;
    const auto fail = [&](std::string_view diagnostic) -> EditorCommandOutcome {
        // Publish terminal truth before fallible feedback/UI work. Committed IO
        // stays observable even if contextual validation or presentation fails.
        task.status = ScenePersistenceTaskStatus::Failed;
        task.diagnostic.assign(diagnostic.data(), std::min<std::size_t>(diagnostic.size(), 256));
        feedback(detail::sceneFileFailureMessage(task.diagnostic));
        presentationState_.openOutput();
        visible("output_panel", true);
        return {true, true, EditorDiagnosticCode::None};
    };
    try {
        if (!file.success) {
            if (task.checkingFile && !file.missing)
                return fail("EDITOR-SCENE-FILE-READ-FAILED: " + file.diagnostic);
            if (file.diagnostic == "ARP-FILE-PRECONDITION-CHANGED"
                || file.diagnostic == "ARP-FILE-PRECONDITION-EXISTS")
                return fail("EDITOR-SCENE-FILE-EXTERNAL-CHANGE");
            if (file.missing || file.diagnostic == "ARP-FILE-PRECONDITION-MISSING")
                return fail("EDITOR-SCENE-FILE-MISSING");
            return fail(file.diagnostic.empty() ? "EDITOR-SCENE-FILE-IO-FAILED" : file.diagnostic);
        }
        core::pmr::String diagnostic, hash;
        if (task.version == EditorSceneScriptCodec::contractVersion && !task.loading) {
            if (task.documentEpoch != sceneDocumentEpoch_
                || !task.owner.sameCurrentEpoch(sceneDocument_.ownerTicket()))
                return fail("EDITOR-SCENE-FILE-STALE-OWNER");
            if (!sceneDocument_.scriptCatalog())
                return fail("EDITOR-SCENE-V4-DOMAIN-UNAVAILABLE");
            if (!sceneFileContextCurrent(
                    task.projectRoot, task.projectModel, task.manifestBytes, diagnostic))
                return fail(diagnostic);
        }
        if (task.checkingFile) {
            if (task.version == EditorSceneScriptCodec::contractVersion) {
                if (!observeSceneHash(hash, diagnostic) || hash != task.contentHash)
                    return fail("EDITOR-SCENE-FILE-PREFLIGHT-STALE");
                core::UniquePtr<EditorSceneScriptFilePreparation::Prepared> token;
                const auto input =
                    EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_);
                if (!EditorSceneScriptFilePreparation::prepare(sceneDocument_,
                        input,
                        {task.relativePath, true, byteText(file.bytes)},
                        token,
                        diagnostic))
                    return fail(diagnostic);
                if (byteText(token->replacementBytes()) != task.saveYaml)
                    return fail("EDITOR-SCENE-FILE-PREFLIGHT-STALE");
                task.request =
                    scenePersistence_.saveScript(sceneDocument_, input, *token, diagnostic);
            } else {
                if (file.bytes != savedSceneFileBytes_)
                    return fail("EDITOR-SCENE-FILE-EXTERNAL-CHANGE");
                task.request = scenePersistence_.saveResolved(task.resolvedTarget, task.saveYaml);
            }
            if (!task.request)
                return fail(diagnostic.empty() ? "EDITOR-SCENE-SAVE-SUBMIT-FAILED" : diagnostic);
            task.checkingFile = false;
            core::pmr::String{}.swap(task.saveYaml);
            return {true, false, EditorDiagnosticCode::None};
        }
        if (!task.loading) {
            if (task.documentEpoch != sceneDocumentEpoch_)
                return fail("EDITOR-SCENE-FILE-STALE-OWNER");
            // Only the submitted content becomes clean. Edits accepted while IO
            // was pending remain dirty; no re-encoding of their values here.
            savedSceneHash_.swap(task.contentHash);
            activeScenePath_.swap(task.relativePath);
            savedSceneFileTarget_.swap(task.resolvedTarget);
            savedSceneFileBytes_.swap(file.bytes);
            task.status = ScenePersistenceTaskStatus::Succeeded;
            return {true, true, EditorDiagnosticCode::None};
        }
        if (task.documentEpoch != sceneDocumentEpoch_
            || !task.owner.sameCurrentEpoch(sceneDocument_.ownerTicket())
            || !observeSceneHash(hash, diagnostic) || hash != task.contentHash)
            return fail("EDITOR-SCENE-LOAD-STALE-DOCUMENT");
        const auto version = EditorSceneFileFormat::version(byteText(file.bytes));
        core::UniquePtr<EditorSceneDocument> candidate;
        if (version == EditorSceneScriptCodec::contractVersion) {
            if (!sceneFileContextCurrent(
                    task.projectRoot, task.projectModel, task.manifestBytes, diagnostic))
                return fail(diagnostic);
            const auto input = EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_);
            if (!EditorSceneScriptCodec::prepare(
                    byteText(file.bytes), sceneDocument_, input, candidate, diagnostic)
                || !EditorSceneScriptCodec::observeContentHash(*candidate, input, hash, diagnostic))
                return fail(diagnostic);
        } else {
            candidate = core::makeUnique<EditorSceneDocument>(false);
            candidate->copySceneServicesFrom(sceneDocument_);
            if (!EditorSceneAuthoringCodec::decode(byteText(file.bytes),
                    *candidate,
                    &EditorSceneAuthoringCodec::isResourceReady,
                    detail::sceneResourceManager(sceneDocument_),
                    diagnostic)
                || !EditorSceneAuthoringCodec::contentHash(*candidate, hash, diagnostic))
                return fail(diagnostic);
        }
        auto replacement = prepareSceneReplacement(*candidate, diagnostic);
        if (!replacement
            || commitSceneReplacement(*replacement, *candidate)
                != SceneReplacementResult::Committed)
            return fail(diagnostic.empty() ? "EDITOR-SCENE-REPLACE-FAILED" : diagnostic);
        // All save-point payloads are already owned; publication cannot allocate.
        sceneAuthoringVersion_ = version;
        savedSceneHash_.swap(hash);
        activeScenePath_.swap(task.relativePath);
        savedSceneFileTarget_.swap(task.resolvedTarget);
        savedSceneFileBytes_.swap(file.bytes);
        ++sceneDocumentEpoch_;
        task.version = version;
        task.status = ScenePersistenceTaskStatus::Succeeded;
        // A visual refresh cannot turn an admitted load back into an IO failure.
    } catch (const core::YamlContractError& error) {
        return fail(error.diagnostic);
    } catch (const std::exception& error) {
        return fail(error.what());
    }
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

} // namespace arp::editor

#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {

struct EditorShell::SourceSceneIntent {
    explicit SourceSceneIntent(const EditorCommandPayload& payload) : command(payload) {}

    EditorCommandPayload command;
    ProjectSourceContext source;
    core::LifetimeEpoch::Ticket owner, history;
    core::pmr::String projectModel, manifest;
    std::uint32_t version = 0;
};

EditorCommandOutcome EditorShell::beginSourceSceneAuthoring(const EditorCommandPayload& payload)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    const auto& directory = projectBrowser_.sourceSnapshot();
    if (!directory || !projectSourceFiles_ || !projectManifest_ || sourceSceneIntent_)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    const bool preparing = std::holds_alternative<PrepareSceneScriptAuthoringCommand>(payload);
    if ((preparing
            && (sceneAuthoringVersion_ != EditorSceneAuthoringCodec::contractVersion
                || sceneDocument_.scriptCatalog() || projectManifest_->scriptModules.empty()))
        || (!preparing && sceneAuthoringVersion_ != EditorSceneScriptCodec::contractVersion)) {
        feedback("EDITOR-SCRIPT-SOURCE-PREPARE-REQUIRED: enable ScriptComponent authoring first.");
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    const SceneScriptConfiguration* fields = nullptr;
    if (const auto* add = std::get_if<AddSceneScriptComponentCommand>(&payload))
        fields = add;
    else if (const auto* edit = std::get_if<SetSceneScriptFieldsCommand>(&payload))
        fields = edit;
    if (fields
        && (fields->fields.size() > EditorSceneScriptPolicy::fields().maxYamlBytes
            || fields->resourceId.size() > EditorSceneScriptPolicy::resourceIdentityBytes
            || fields->scriptType.size() > EditorSceneScriptPolicy::scriptTypeBytes))
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    try {
        auto intent = core::makeUnique<SourceSceneIntent>(payload);
        intent->source = {
            directory->project(), projectSourceFiles_, directory->project()->location().epoch};
        intent->owner = sceneDocument_.ownerTicket();
        intent->history = sceneHistoryEpoch_.ticket();
        intent->version = sceneAuthoringVersion_;
        ProjectDiagnostic issue;
        if (!ProjectManifestCodec::encode(*projectManifest_, intent->projectModel, issue))
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        intent->manifest = projectResources_.manifestSnapshot();
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        auto result = acceptProjectIoTask(
            core::makeUnique<EditorProjectIoTask>(intent->source, projectIoBudget_), id);
        if (result.success)
            sourceSceneIntent_.swap(intent);
        return result;
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

void EditorShell::retireSourceSceneIntent(bool success) noexcept
{
    if (!sourceSceneIntent_)
        return;
    scriptInspector_.sourceTaskFinished(*this, projectIoTaskId_, success);
    sourceSceneIntent_.reset();
}

EditorCommandOutcome EditorShell::prepareSourceSceneScriptDomain()
{
    if (!sourceSceneApplying_ || !sourceSceneAdmission_ || !projectManifest_
        || sceneAuthoringVersion_ != EditorSceneAuthoringCodec::contractVersion
        || sceneDocument_.scriptCatalog())
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    core::pmr::String yaml, diagnostic, verified;
    if (!EditorSceneAuthoringCodec::encode(sceneDocument_, yaml, diagnostic))
        return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
    EditorSceneDocument candidate(false);
    candidate.copySceneServicesFrom(sceneDocument_);
    const auto provider = sceneDocument_.resourceManagerProvider();
    if (!EditorSceneAuthoringCodec::decode(yaml,
            candidate,
            &EditorSceneAuthoringCodec::isResourceReady,
            provider ? provider() : nullptr,
            diagnostic)
        || !candidate.prepareScriptAuthoringSources(*sourceSceneAdmission_,
            *projectManifest_,
            EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_).modules,
            diagnostic))
        return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
    auto input = EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_);
    input.sources = sourceSceneAdmission_;
    if (!EditorSceneScriptCodec::encode(candidate, input, verified, diagnostic))
        return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
    auto replacement = prepareSceneReplacement(candidate, diagnostic);
    const char* issue = nullptr;
    if (!replacement || !sourceSceneAdmission_->validate(issue)
        || commitSceneReplacement(*replacement, candidate) != SceneReplacementResult::Committed)
        return {false, false, EditorDiagnosticCode::SceneHistoryRestoreRejected};
    // Explicit v3 -> v4 document owner transition. The old raw save point stays
    // intact and therefore dirty; enabling authoring neither saves nor runs code.
    sceneAuthoringVersion_ = EditorSceneScriptCodec::contractVersion;
    ++sceneDocumentEpoch_;
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::completeSourceSceneAuthoring()
{
    const auto fail = [&](const char* issue) {
        projectIo_->fail(issue);
        retireSourceSceneIntent(false);
        return EditorCommandOutcome{true, true, EditorDiagnosticCode::None};
    };
    if (!sourceSceneIntent_)
        return fail("EDITOR-SCRIPT-SOURCE-INTENT-MISSING");
    const auto& intent = *sourceSceneIntent_;
    const auto& directory = projectBrowser_.sourceSnapshot();
    const char* issue = nullptr;
    if (!directory || directory->project() != intent.source.snapshot
        || projectIo_->verifiedSources().snapshot != intent.source.snapshot
        || !intent.owner.sameCurrentEpoch(sceneDocument_.ownerTicket())
        || !intent.history.sameCurrentEpoch(sceneHistoryEpoch_.ticket())
        || intent.version != sceneAuthoringVersion_ || !intent.source.validate(issue))
        return fail(issue ? issue : "EDITOR-SCRIPT-SOURCE-INTENT-STALE");
    ProjectDiagnostic problem;
    core::pmr::String model;
    if (!projectManifest_ || !ProjectManifestCodec::encode(*projectManifest_, model, problem)
        || model != intent.projectModel || projectResources_.manifestSnapshot() != intent.manifest)
        return fail("EDITOR-SCRIPT-SOURCE-PROJECT-STALE");
    EditorCommandOutcome outcome;
    {
        // Only this private, already verified owner intent can enter mounted
        // archive/history mutation. Nested presentation is deferred until its
        // terminal receipt is published; callbacks never set this authority.
        struct Application {
            EditorShell& shell;

            explicit Application(EditorShell& owner, const ProjectSourceContext& source)
                : shell(owner)
            {
                shell.sourceSceneApplying_ = true;
                shell.sourceSceneAdmission_ = &source;
            }

            ~Application()
            {
                shell.sourceSceneAdmission_ = nullptr;
                shell.sourceSceneApplying_ = false;
            }
        } applying(*this, intent.source);

        outcome = execute(intent.command);
    }
    if (!outcome.success)
        return fail("EDITOR-SCRIPT-SOURCE-MUTATION-REJECTED");
    projectIo_->published();
    retireSourceSceneIntent(true);
    // Presentation can fail after a successful mutation; never relabel that as
    // a failed operation or replay the authoring/history command.
    try {
        sceneInspectorWidgetOwner_ = {};
        refreshSceneInspector();
    } catch (...) {
        failSceneInspector("EDITOR-SCRIPT-SOURCE-PRESENTATION");
    }
    return {true, true, EditorDiagnosticCode::None};
}
} // namespace arp::editor

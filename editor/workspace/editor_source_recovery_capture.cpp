#include "editor/workspace/editor_source_recovery_capture.h"

#include "core/io/filesystem_path.h"
#include "editor/workspace/editor_scene_script_policy.h"

namespace arp::editor {
struct EditorSourceRecoveryCapture::State {
    ProjectSourceContext context;
    EditorRecoverySnapshot snapshot;
    core::UniquePtr<ProjectSourceVerification> verification;
    SourceRecoveryCaptureState phase = SourceRecoveryCaptureState::Pending;
    core::pmr::String detail;
    const char* diagnostic = nullptr;

    void fail(const char* issue) noexcept
    {
        diagnostic = issue ? issue : (detail.empty() ? "EDITOR-RECOVERY-SOURCE-CAPTURE" : nullptr);
        phase = SourceRecoveryCaptureState::Failed;
        verification.reset();
        snapshot = {};
        context = {};
    }

    bool capture(const EditorSourceRecoveryCaptureInput& input)
    {
        const auto* saved = input.resources.manifest();
        const auto services = input.document.sceneServiceSession();
        const char* issue = nullptr;
        if (!input.sources.validate(issue)) {
            diagnostic = issue;
            return false;
        }
        if (!saved || input.resources.sourceSnapshot() != input.sources.snapshot
            || !input.resources.packageProvenanceBound() || !services
            || services->resources() != &input.resources.manager()
            || services->physics() != input.resources.physicsWorld2D()
            || !services->ownerTicket().isCurrent() || !input.document.ownerTicket().isCurrent()
            || !ProjectManifestCodec::isSettingsOnlyChange(*saved, input.projectDraft)) {
            diagnostic = "EDITOR-RECOVERY-SOURCE-CAPTURE-OWNER-OR-DRAFT";
            return false;
        }
        if (!input.projectDraft.entryScene.empty()
            && !EditorSceneAuthoringCodec::isResourceReady(
                services->resources(), "SceneAssetResource", input.projectDraft.entryScene)) {
            diagnostic = "EDITOR-RECOVERY-SOURCE-CAPTURE-ENTRY";
            return false;
        }
        const auto* manifest = input.sources.snapshot->find("project.arp.yaml");
        if (!manifest) {
            diagnostic = "EDITOR-RECOVERY-SOURCE-CAPTURE-MANIFEST";
            return false;
        }
        snapshot.sessionId = input.sessionId;
        snapshot.sequence = input.sequence;
        snapshot.workspaceEpoch = input.workspaceEpoch;
        snapshot.workspace = input.workspace;
        snapshot.source.emplace();
        snapshot.source->alias = input.alias;
        snapshot.source->root = input.sources.snapshot->location().root;
        const auto& bytes = manifest->receipt.bytes;
        snapshot.source->manifestFileBytes.assign(
            bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data()), bytes.size());
        snapshot.source->inputs = input.sources.snapshot->identities();
        snapshot.project.emplace();
        auto& project = *snapshot.project;
        project.id = saved->id;
        project.savedFileBytes = input.resources.manifestSnapshot();
        ProjectDiagnostic codec;
        if (!ProjectManifestCodec::encode(input.projectDraft, project.draftYaml, codec)) {
            diagnostic = codec.code;
            return false;
        }
        project.packages = input.resources.packageBindings();
        project.inputFiles = input.resources.inputBindings();
        if (!saved->editorMetadata.empty())
            project.metadata = EditorRecoveryMetadata{
                saved->editorMetadata, core::pmr::String(input.resources.metadataSnapshot())};
        auto& scene = snapshot.scene;
        scene.path = scene.boundTarget = input.scenePath;
        scene.savedContentHash = input.sceneSavedHash;
        scene.savedFileBytes = input.sceneSavedFileBytes;
        const auto root = std::filesystem::u8path(snapshot.source->root);
        if (input.sceneVersion == EditorSceneAuthoringCodec::contractVersion) {
            if (!EditorSceneAuthoringCodec::encode(input.document, scene.yaml, detail))
                return false;
        } else if (input.sceneVersion == EditorSceneScriptCodec::contractVersion) {
            auto inputs = EditorSceneScriptPolicy::inputs(root, input.projectDraft);
            inputs.sources = &input.sources;
            if (!EditorSceneScriptCodec::observeSnapshot(
                    input.document, inputs, scene.yaml, detail))
                return false;
        } else {
            diagnostic = "EDITOR-RECOVERY-SOURCE-CAPTURE-SCENE-VERSION";
            return false;
        }
        for (const auto& document : input.scripts.documents()) {
            const auto* location = document.sourceLocation();
            const auto& expected = input.sources.snapshot->location();
            if (!location || !(location->mount == expected.mount) || location->root != expected.root
                || location->epoch != expected.epoch
                || !document.validateSource(*input.sources.files, input.sources.epoch, issue)) {
                diagnostic = issue ? issue : "EDITOR-RECOVERY-SOURCE-CAPTURE-SCRIPT-ORIGIN";
                return false;
            }
            EditorRecoveryScript script;
            script.path = root.empty()
                ? core::FilePath::utf8(document.path)
                : core::FilePath::utf8(document.path.lexically_relative(root));
            if (expected.pathFor(script.path) != core::FilePath::utf8(document.path)) {
                diagnostic = "EDITOR-RECOVERY-SOURCE-CAPTURE-SCRIPT-PATH";
                return false;
            }
            script.text = document.text;
            script.savedText = document.savedText;
            script.savedFileBytes = document.savedFileBytes;
            script.newline = document.newline;
            script.cursor = document.cursor;
            script.anchor = document.anchor;
            script.scrollLine = document.scrollLine;
            snapshot.scripts.push_back(std::move(script));
        }
        if (input.scripts.activeIndex() != ScriptDocumentSession::noActiveDocument)
            snapshot.activeScript = input.scripts.activeIndex();
        return true;
    }
};

EditorSourceRecoveryCapture::EditorSourceRecoveryCapture(
    const EditorSourceRecoveryCaptureInput& input,
    ProjectInputBudget inputs,
    EditorRecoveryBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& s = *state_;
    if (input.scripts.size() > budget.maxScripts || !s.capture(input)
        || !EditorRecoveryCodec::validate(s.snapshot, s.detail, budget)) {
        s.fail(s.diagnostic);
        return;
    }
    s.context = input.sources;
    s.verification = core::makeUnique<ProjectSourceVerification>(s.context, inputs);
}

EditorSourceRecoveryCapture::~EditorSourceRecoveryCapture() = default;

SourceRecoveryCaptureState EditorSourceRecoveryCapture::state() const noexcept
{
    return state_->phase;
}

std::string_view EditorSourceRecoveryCapture::diagnostic() const noexcept
{
    return state_->diagnostic ? std::string_view(state_->diagnostic)
                              : std::string_view(state_->detail);
}

const ProjectSourceContext& EditorSourceRecoveryCapture::context() const noexcept
{
    return state_->context;
}

const EditorRecoverySnapshot* EditorSourceRecoveryCapture::snapshot(
    std::uint64_t currentEpoch, const char*& diagnostic) const noexcept
{
    diagnostic = "EDITOR-RECOVERY-SOURCE-CAPTURE-NOT-READY-OR-EPOCH";
    if (state_->phase != SourceRecoveryCaptureState::Ready || currentEpoch != state_->context.epoch
        || !state_->context.validate(diagnostic))
        return nullptr;
    diagnostic = nullptr;
    return &state_->snapshot;
}

void EditorSourceRecoveryCapture::cancel() noexcept
{
    if (state_->phase == SourceRecoveryCaptureState::Pending) {
        state_->fail("EDITOR-RECOVERY-SOURCE-CAPTURE-CANCELLED");
        state_->phase = SourceRecoveryCaptureState::Cancelled;
    }
}

void EditorSourceRecoveryCapture::advance(std::uint64_t currentEpoch) noexcept
{
    auto& s = *state_;
    if (s.phase != SourceRecoveryCaptureState::Pending)
        return;
    if (currentEpoch != s.context.epoch) {
        s.fail("EDITOR-RECOVERY-SOURCE-CAPTURE-EPOCH");
        return;
    }
    s.verification->advance();
    if (s.verification->state() == ProjectSourceVerificationState::Verified) {
        s.verification.reset();
        s.phase = SourceRecoveryCaptureState::Ready;
    } else if (s.verification->state() != ProjectSourceVerificationState::Pending)
        s.fail(s.verification->diagnostic());
}
} // namespace arp::editor

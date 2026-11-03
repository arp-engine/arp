#include "editor/workspace/editor_source_recovery_preparation.h"

#include "core/hash/sha256.h"
#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_service_session.h"

#include <algorithm>

namespace arp::editor {
namespace {
core::StringView bytesOf(const ProjectSourceSnapshot::Entry& entry) noexcept
{
    const auto& bytes = entry.receipt.bytes;
    return {bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace

struct EditorSourceRecoveryPreparation::State {
    core::io::FileService* files = nullptr;
    ProjectSourceLocation location;
    SourceRecoveryState phase = SourceRecoveryState::Pending;
    const char* issue = nullptr;
    core::pmr::String detail;
    core::UniquePtr<Candidate> prepared;
    core::UniquePtr<ProjectSourceSession> acquisition;
    std::size_t next = 0;
    bool reading = false;

    void fail(const char* diagnostic) noexcept
    {
        phase = SourceRecoveryState::Failed;
        issue = diagnostic ? diagnostic : "EDITOR-RECOVERY-SOURCE-ACQUISITION";
        acquisition.reset();
        prepared.reset();
    }

    bool restore(std::string_view yaml, EditorSceneDocument& document)
    {
        if (EditorSceneFileFormat::version(yaml) == EditorSceneAuthoringCodec::contractVersion)
            return EditorSceneAuthoringCodec::decode(yaml,
                document,
                &EditorSceneAuthoringCodec::isResourceReady,
                &prepared->resources.manager(),
                detail);
        const auto root = std::filesystem::u8path(location.root);
        auto inputs = EditorSceneScriptPolicy::inputs(root, prepared->draft);
        inputs.sources = &prepared->sources;
        return EditorSceneScriptCodec::decode(yaml, document, inputs, detail);
    }

    bool prepareDocument()
    {
        auto& candidate = *prepared;
        const auto& snapshot = candidate.submitted;
        const auto loaded =
            candidate.resources.loadSources(candidate.sources.snapshot, *files, location.epoch);
        if (!loaded.success) {
            detail = loaded.diagnostics;
            return false;
        }
        const auto& project = *snapshot.project;
        if (candidate.resources.manifestSnapshot() != project.savedFileBytes
            || !candidate.resources.packageProvenanceBound()
            || !ProjectPackageSnapshot::equal(
                project.packages, candidate.resources.packageBindings())
            || !ProjectInputSnapshot::equal(project.inputFiles, candidate.resources.inputBindings())
            || candidate.resources.metadataSnapshot()
                != (project.metadata ? core::StringView(project.metadata->fileBytes)
                                     : core::StringView{})) {
            issue = "EDITOR-RECOVERY-SOURCE-RESOURCE-CLOSURE";
            return false;
        }
        if (!candidate.draft.entryScene.empty()
            && !EditorSceneAuthoringCodec::isResourceReady(
                &candidate.resources.manager(), "SceneAssetResource", candidate.draft.entryScene)) {
            issue = "EDITOR-RECOVERY-DRAFT-ENTRY-NOT-READY";
            return false;
        }
        const auto services = core::makeShared<EditorSceneServiceSession>(
            candidate.resources.retainManager(), candidate.resources.physicsWorld2D());
        const auto document = [&] {
            auto result = core::makeUnique<EditorSceneDocument>(false);
            if (!result->bindServiceSession(services, detail))
                return core::UniquePtr<EditorSceneDocument>{};
            return result;
        };
        if (!snapshot.scene.path.empty()) {
            auto baseline = document();
            if (!baseline || !restore(snapshot.scene.savedFileBytes, *baseline))
                return false;
            core::pmr::String hash;
            const auto version = EditorSceneFileFormat::version(snapshot.scene.savedFileBytes);
            const auto root = std::filesystem::u8path(location.root);
            auto inputs = EditorSceneScriptPolicy::inputs(root, candidate.draft);
            inputs.sources = &candidate.sources;
            const auto valid = version == EditorSceneAuthoringCodec::contractVersion
                ? EditorSceneAuthoringCodec::contentHash(*baseline, hash, detail)
                : EditorSceneScriptCodec::observeContentHash(*baseline, inputs, hash, detail);
            if (!valid)
                return false;
            if (hash != snapshot.scene.savedContentHash) {
                issue = "EDITOR-RECOVERY-SCENE-SAVEPOINT-MISMATCH";
                return false;
            }
        }
        candidate.document = document();
        return candidate.document && restore(snapshot.scene.yaml, *candidate.document);
    }
};

EditorSourceRecoveryPreparation::EditorSourceRecoveryPreparation(core::io::FileService& files,
    const EditorRecoverySnapshot& snapshot,
    std::string_view admittedAlias,
    ProjectSourceLocation location,
    ProjectInputBudget inputs,
    EditorRecoveryBudget recovery)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.files = &files;
    state.location = std::move(location);
    if (!EditorRecoveryCodec::validate(snapshot, state.detail, recovery)) {
        state.fail("EDITOR-RECOVERY-SOURCE-RECORD");
        return;
    }
    if (!snapshot.source || !snapshot.project || !state.location.mount || !state.location.epoch
        || snapshot.source->alias != admittedAlias
        || snapshot.source->root != state.location.root) {
        state.fail("EDITOR-RECOVERY-SOURCE-NOT-ADMITTED");
        return;
    }
    if (!ProjectInputSnapshot::validateBindings(snapshot.source->inputs, state.detail, inputs)) {
        state.fail("EDITOR-RECOVERY-SOURCE-INPUT-BUDGET");
        return;
    }
    ProjectManifest saved, draft;
    ProjectDiagnostic diagnostic;
    if (!ProjectManifestCodec::decode(snapshot.project->savedFileBytes, saved, diagnostic)
        || !ProjectManifestCodec::decode(snapshot.project->draftYaml, draft, diagnostic)
        || !ProjectManifestCodec::isSettingsOnlyChange(saved, draft)) {
        state.fail("EDITOR-RECOVERY-PROJECT-DRAFT-CONTRACT");
        return;
    }
    state.prepared = core::makeUnique<Candidate>();
    state.prepared->submitted = snapshot;
    state.prepared->draft = std::move(draft);
    state.acquisition = core::makeUnique<ProjectSourceSession>(files, state.location, inputs);
}

EditorSourceRecoveryPreparation::~EditorSourceRecoveryPreparation() = default;

SourceRecoveryState EditorSourceRecoveryPreparation::state() const noexcept
{
    return state_->phase;
}

std::string_view EditorSourceRecoveryPreparation::diagnostic() const noexcept
{
    return state_->issue ? std::string_view(state_->issue) : std::string_view(state_->detail);
}

std::string_view EditorSourceRecoveryPreparation::diagnosticDetail() const noexcept
{
    return state_->detail;
}

const EditorSourceRecoveryPreparation::Candidate* EditorSourceRecoveryPreparation::candidate(
    std::uint64_t currentEpoch, const char*& diagnostic) const noexcept
{
    diagnostic = nullptr;
    if (state_->phase != SourceRecoveryState::Ready || !state_->prepared
        || currentEpoch != state_->location.epoch) {
        diagnostic = "EDITOR-RECOVERY-SOURCE-NOT-READY-OR-EPOCH";
        return nullptr;
    }
    if (!state_->prepared->sources.validate(diagnostic))
        return nullptr;
    return state_->prepared.get();
}

void EditorSourceRecoveryPreparation::cancel() noexcept
{
    if (state_->phase == SourceRecoveryState::Pending) {
        state_->fail("EDITOR-RECOVERY-SOURCE-CANCELLED");
        state_->phase = SourceRecoveryState::Cancelled;
    }
}

core::UniquePtr<EditorSourceRecoveryPreparation::Candidate>
EditorSourceRecoveryPreparation::takeCandidate(
    std::uint64_t currentEpoch, const char*& diagnostic) noexcept
{
    if (!candidate(currentEpoch, diagnostic))
        return {};
    state_->phase = SourceRecoveryState::Consumed;
    return std::move(state_->prepared);
}

void EditorSourceRecoveryPreparation::advance(std::uint64_t currentEpoch) noexcept
{
    auto& state = *state_;
    if (state.phase != SourceRecoveryState::Pending)
        return;
    try {
        if (currentEpoch != state.location.epoch) {
            state.fail("EDITOR-RECOVERY-SOURCE-EPOCH");
            return;
        }
        auto& session = *state.acquisition;
        if (session.state() == ProjectSourceState::Pending)
            return;
        if (session.state() != ProjectSourceState::Ready) {
            state.fail(session.diagnostic());
            return;
        }
        const auto& source = *state.prepared->submitted.source;
        if (state.reading) {
            const auto& expected = source.inputs[state.next];
            const auto* acquired = session.acquired(expected.path);
            constexpr std::string_view fingerprintPrefix = "sha256:";
            if (!acquired || acquired->receipt.bytes.size() != expected.byteCount
                || !core::Sha256::matchesHexDigest(bytesOf(*acquired),
                    std::string_view(expected.fingerprint).substr(fingerprintPrefix.size()))) {
                state.fail("EDITOR-RECOVERY-SOURCE-INPUT-CONFLICT");
                return;
            }
            ++state.next;
            state.reading = false;
        }
        if (state.next != source.inputs.size()) {
            const auto& input = source.inputs[state.next];
            // Empty files still require a finite positive IO budget. Compare
            // the exact count/hash before accepting the next input or parsing.
            if (session.read(input.path, std::max<std::size_t>(1, input.byteCount))
                != core::io::FileIoStatus::Accepted)
                state.fail(session.diagnostic());
            else
                state.reading = true;
            return;
        }
        auto acquired = session.finish(currentEpoch);
        if (!acquired) {
            state.fail(session.diagnostic());
            return;
        }
        const auto* manifest = acquired->find("project.arp.yaml");
        if (!manifest || !ProjectInputSnapshot::equal(acquired->identities(), source.inputs)
            || bytesOf(*manifest) != source.manifestFileBytes) {
            state.fail("EDITOR-RECOVERY-SOURCE-INPUT-CLOSURE");
            return;
        }
        state.prepared->sources = {std::move(acquired), state.files, currentEpoch};
        state.acquisition.reset();
        if (!state.prepareDocument()) {
            state.fail(state.issue ? state.issue : "EDITOR-RECOVERY-SOURCE-COLD-PREPARATION");
            return;
        }
        const char* diagnostic = nullptr;
        if (!state.prepared->sources.validate(diagnostic)) {
            state.fail(diagnostic);
            return;
        }
        state.phase = SourceRecoveryState::Ready;
    } catch (...) {
        state.fail("EDITOR-RECOVERY-SOURCE-PREPARATION-FAILED");
    }
}

} // namespace arp::editor

#include "editor/workspace/editor_recovery_preparation.h"

#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "editor/project/project_scene_authoring.h"
#include "editor/workspace/editor_recovery_digest.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/editor_scene_service_session.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>

namespace arp::editor {
namespace {
using Preparation = EditorRecoveryPreparation;

bool fail(core::pmr::String& diagnostic, const char* code)
{
    diagnostic = code;
    return false;
}

bool admittedRoot(const std::filesystem::path& root)
{
    std::error_code error;
    return root.is_absolute() && root == root.lexically_normal()
        && std::filesystem::canonical(root, error) == root && !error
        && std::filesystem::is_directory(root, error) && !error;
}

bool resolve(const std::filesystem::path& root,
    std::string_view relative,
    ProjectPathIntent intent,
    std::filesystem::path& path,
    core::pmr::String& diagnostic)
{
    ProjectDiagnostic issue;
    if (!ProjectManifestCodec::resolvePath(root, relative, intent, path, issue))
        return fail(diagnostic, issue.code);
    return true;
}

bool observeFile(const std::filesystem::path& path,
    std::string_view relative,
    std::size_t maxBytes,
    Preparation::FormalFileObservation& observation,
    core::pmr::String& diagnostic)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    observation.relativePath = relative;
    observation.target = path;
    if (status.type() == std::filesystem::file_type::not_found
        && (!error || error == std::errc::no_such_file_or_directory)) {
        observation.bytes.reset();
        return true;
    }
    if (error || status.type() != std::filesystem::file_type::regular)
        return fail(diagnostic, "EDITOR-RECOVERY-BASELINE-FILE-TYPE");
    auto read = core::readTextFile(path, maxBytes);
    if (!read.success)
        return fail(diagnostic, "EDITOR-RECOVERY-BASELINE-READ");
    observation.bytes = std::move(read.text);
    return true;
}

bool fileBaseline(const std::filesystem::path& path,
    std::string_view relative,
    std::string_view baseline,
    std::size_t maxBytes,
    Preparation::Candidate& candidate,
    core::pmr::String& diagnostic)
{
    Preparation::FormalFileObservation observation;
    if (!observeFile(path, relative, maxBytes, observation, diagnostic))
        return false;
    std::size_t remaining = Preparation::maxObservedDocumentBytes;
    for (const auto& file : candidate.files)
        if (file.bytes) {
            if (file.bytes->size() > remaining)
                return fail(diagnostic, "EDITOR-RECOVERY-OBSERVATION-BUDGET");
            remaining -= file.bytes->size();
        }
    if (observation.bytes && observation.bytes->size() > remaining)
        return fail(diagnostic, "EDITOR-RECOVERY-OBSERVATION-BUDGET");
    if (!observation.bytes)
        candidate.conflicts.push_back(
            {core::pmr::String(relative), Preparation::ConflictKind::Missing});
    else if (*observation.bytes != baseline)
        candidate.conflicts.push_back(
            {core::pmr::String(relative), Preparation::ConflictKind::Changed});
    candidate.files.push_back(std::move(observation));
    return true;
}

bool noProjectContext(const std::filesystem::path& root, core::pmr::String& diagnostic)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(root / "project.arp.yaml", error);
    if (status.type() != std::filesystem::file_type::not_found
        || (error && error != std::errc::no_such_file_or_directory))
        return fail(diagnostic, "EDITOR-RECOVERY-PROJECT-CONTEXT-CHANGED");
    return true;
}

bool verifyInputs(const std::filesystem::path& root,
    const EditorRecoveryProject& project,
    core::pmr::String& diagnostic)
{
    if (project.metadata) {
        std::filesystem::path path;
        if (!resolve(
                root, project.metadata->path, ProjectPathIntent::ExistingFile, path, diagnostic))
            return false;
        const auto read = core::readTextFile(path, EditorRecoveryCodec::maxMetadataBytes);
        if (!read.success || read.text != project.metadata->fileBytes)
            return fail(diagnostic, "EDITOR-RECOVERY-METADATA-BASELINE-CONFLICT");
    }
    return ProjectInputSnapshot::verify(root, project.inputFiles, diagnostic);
}

bool sameSettingsContract(
    ProjectManifest saved, const ProjectManifest& draft, core::pmr::String& diagnostic)
{
    // These are the only currently supported Editor settings draft fields.
    // Accepting a different physics/module/resource contract would synthesize
    // owners that were never bound to this recovery record.
    saved.name = draft.name;
    saved.entryScene = draft.entryScene;
    core::pmr::String left, right;
    ProjectDiagnostic issue;
    return ProjectManifestCodec::encode(saved, left, issue)
            && ProjectManifestCodec::encode(draft, right, issue) && left == right
        ? true
        : fail(diagnostic, "EDITOR-RECOVERY-PROJECT-DRAFT-CONTRACT");
}
} // namespace

bool EditorRecoveryPreparation::capturePackages(const std::filesystem::path& root,
    std::string_view manifestYaml,
    core::pmr::Vector<EditorRecoveryPackage>& destination,
    core::pmr::String& diagnostic)
{
    try {
        ProjectManifest manifest;
        ProjectDiagnostic issue;
        if (!admittedRoot(root) || !ProjectManifestCodec::decode(manifestYaml, manifest, issue))
            return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-ROOT-OR-MANIFEST");
        EditorProjectResources inspection;
        const auto factory = ProjectSceneAuthoring::factory(inspection.manager(), {}, diagnostic);
        const auto codecs =
            factory ? ProjectSceneAuthoring::packageCodecs(factory, diagnostic) : nullptr;
        return codecs
            && ProjectPackageSnapshot::capture(
                root, manifest.assetPackages, codecs, destination, diagnostic);
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-PREPARATION-FAILED");
    }
}

bool EditorRecoveryPreparation::captureInputs(const std::filesystem::path& root,
    const EditorProjectResources& resources,
    EditorRecoveryProject& destination,
    core::pmr::String& diagnostic)
{
    try {
        const auto* manifest = resources.manifest();
        if (!admittedRoot(root) || !manifest || destination.id != manifest->id
            || destination.savedFileBytes != resources.manifestSnapshot())
            return fail(diagnostic, "EDITOR-RECOVERY-INPUT-OWNER-MISMATCH");
        if (!resources.packageProvenanceBound()) {
            diagnostic = "EDITOR-RECOVERY-PACKAGE-OWNER-UNBOUND: ";
            diagnostic += resources.packageProvenanceDiagnostic();
            return false;
        }
        core::pmr::Vector<EditorRecoveryPackage> packages;
        if (!capturePackages(root, resources.manifestSnapshot(), packages, diagnostic))
            return false;
        if (!ProjectPackageSnapshot::equal(resources.packageBindings(), packages))
            return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-OWNER-STALE");
        auto candidate = destination;
        candidate.packages = resources.packageBindings();
        candidate.metadata.reset();
        if (!manifest->editorMetadata.empty())
            candidate.metadata = EditorRecoveryMetadata{
                manifest->editorMetadata, core::pmr::String(resources.metadataSnapshot())};
        candidate.inputFiles = resources.inputBindings();
        if (!verifyInputs(root, candidate, diagnostic))
            return false;
        destination = std::move(candidate);
        diagnostic.clear();
        return true;
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-INPUT-CAPTURE-FAILED");
    }
}

bool EditorRecoveryPreparation::validateFiles(const Candidate& candidate,
    const std::filesystem::path& currentRoot,
    core::pmr::String& diagnostic)
{
    try {
        if (!admittedRoot(currentRoot) || currentRoot != candidate.root
            || core::FilePath::utf8(currentRoot) != candidate.submitted.projectRoot)
            return fail(diagnostic, "EDITOR-RECOVERY-FILE-CONTEXT-STALE");
        const auto& snapshot = candidate.submitted;
        if (snapshot.project) {
            if (!candidate.resources.packageProvenanceBound()
                || !ProjectPackageSnapshot::equal(
                    snapshot.project->packages, candidate.resources.packageBindings()))
                return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-OWNER-MISMATCH");
            std::filesystem::path path;
            if (!resolve(currentRoot,
                    "project.arp.yaml",
                    ProjectPathIntent::ExistingFile,
                    path,
                    diagnostic))
                return false;
            const auto read = core::readTextFile(path, EditorProjectResources::maxManifestBytes);
            if (!read.success || read.text != snapshot.project->savedFileBytes)
                return fail(diagnostic, "EDITOR-RECOVERY-PROJECT-BASELINE-CONFLICT");
            if (!verifyInputs(currentRoot, *snapshot.project, diagnostic))
                return false;
            core::pmr::Vector<EditorRecoveryPackage> packages;
            if (!capturePackages(
                    currentRoot, snapshot.project->savedFileBytes, packages, diagnostic))
                return false;
            if (!ProjectPackageSnapshot::equal(snapshot.project->packages, packages))
                return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-BINDING-CONFLICT");
        } else if (!noProjectContext(currentRoot, diagnostic))
            return false;
        if (candidate.document) {
            const auto catalog = candidate.document->scriptCatalog();
            if (EditorSceneFileFormat::version(snapshot.scene.yaml)
                    == EditorSceneScriptCodec::contractVersion
                && (!catalog || !catalog->modules().verifySources(currentRoot, diagnostic)))
                return fail(diagnostic, "EDITOR-RECOVERY-SCRIPT-SOURCE-OR-OWNER-STALE");
        }
        const auto expectedCount =
            snapshot.scripts.size() + (snapshot.scene.path.empty() ? 0u : 1u);
        if (candidate.files.size() != expectedCount)
            return fail(diagnostic, "EDITOR-RECOVERY-OBSERVATION-SET-INVALID");
        std::size_t remaining = maxObservedDocumentBytes;
        for (std::size_t index = 0; index < candidate.files.size(); ++index) {
            const auto& previous = candidate.files[index];
            const bool isScene = !snapshot.scene.path.empty() && index == 0u;
            const auto& expectedPath = isScene
                ? snapshot.scene.path
                : snapshot.scripts[index - (snapshot.scene.path.empty() ? 0u : 1u)].path;
            if (previous.relativePath != expectedPath)
                return fail(diagnostic, "EDITOR-RECOVERY-OBSERVATION-SET-INVALID");
            if (previous.bytes) {
                if (previous.bytes->size() > remaining)
                    return fail(diagnostic, "EDITOR-RECOVERY-OBSERVATION-BUDGET");
                remaining -= previous.bytes->size();
            }
            std::filesystem::path target;
            if (!resolve(currentRoot,
                    previous.relativePath,
                    ProjectPathIntent::NewFile,
                    target,
                    diagnostic))
                return false;
            if (target != previous.target)
                return fail(diagnostic, "EDITOR-RECOVERY-DOCUMENT-TARGET-STALE");
            FormalFileObservation current;
            if (!observeFile(target,
                    previous.relativePath,
                    isScene ? EditorSceneAuthoringCodec::maxDocumentBytes
                            : ScriptDocumentSession::maxScriptBytes,
                    current,
                    diagnostic))
                return false;
            if (current.bytes != previous.bytes)
                return fail(diagnostic, "EDITOR-RECOVERY-DOCUMENT-BYTES-STALE");
        }
        diagnostic.clear();
        return true;
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-FRESHNESS-FAILED");
    }
}

core::UniquePtr<EditorRecoveryPreparation::Candidate> EditorRecoveryPreparation::prepare(
    const EditorRecoverySnapshot& snapshot,
    const std::filesystem::path& root,
    core::pmr::String& diagnostic,
    EditorRecoveryBudget budget)
{
    if (!EditorRecoveryCodec::validate(snapshot, diagnostic, budget))
        return {};
    try {
        if (!admittedRoot(root) || core::FilePath::utf8(root) != snapshot.projectRoot) {
            fail(diagnostic, "EDITOR-RECOVERY-ROOT-NOT-ADMITTED");
            return {};
        }
        auto candidate = core::makeUnique<Candidate>();
        candidate->submitted = snapshot;
        candidate->root = root;
        ProjectManifest draft;
        if (snapshot.project) {
            const auto& project = *snapshot.project;
            ProjectManifest saved;
            ProjectDiagnostic issue;
            std::filesystem::path manifestPath;
            if (!resolve(root,
                    "project.arp.yaml",
                    ProjectPathIntent::ExistingFile,
                    manifestPath,
                    diagnostic))
                return {};
            const auto current =
                core::readTextFile(manifestPath, EditorProjectResources::maxManifestBytes);
            if (!current.success || current.text != project.savedFileBytes) {
                fail(diagnostic, "EDITOR-RECOVERY-PROJECT-BASELINE-CONFLICT");
                return {};
            }
            if (!ProjectManifestCodec::decode(project.savedFileBytes, saved, issue)
                || !ProjectManifestCodec::decode(project.draftYaml, draft, issue)
                || !sameSettingsContract(saved, draft, diagnostic))
                return {};
            if (!verifyInputs(root, project, diagnostic))
                return {};
            core::pmr::Vector<EditorRecoveryPackage> packages;
            if (!capturePackages(root, project.savedFileBytes, packages, diagnostic))
                return {};
            if (!ProjectPackageSnapshot::equal(project.packages, packages)) {
                fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-BINDING-CONFLICT");
                return {};
            }
            const auto loaded = candidate->resources.loadManifest(root, project.savedFileBytes);
            if (!loaded.success) {
                diagnostic = loaded.diagnostics;
                return {};
            }
            if (!candidate->resources.packageProvenanceBound()
                || !ProjectPackageSnapshot::equal(
                    project.packages, candidate->resources.packageBindings())) {
                fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-OWNER-MISMATCH");
                return {};
            }
            if (candidate->resources.metadataSnapshot()
                    != (project.metadata ? std::string_view(project.metadata->fileBytes)
                                         : std::string_view{})
                || !ProjectInputSnapshot::equal(
                    project.inputFiles, candidate->resources.inputBindings())) {
                fail(diagnostic, "EDITOR-RECOVERY-INPUT-CLOSURE-MISMATCH");
                return {};
            }
            if (!verifyInputs(root, project, diagnostic))
                return {};
            if (!draft.entryScene.empty()
                && !EditorSceneAuthoringCodec::isResourceReady(
                    &candidate->resources.manager(), "SceneAssetResource", draft.entryScene)) {
                fail(diagnostic, "EDITOR-RECOVERY-DRAFT-ENTRY-NOT-READY");
                return {};
            }
            // Revalidate explicit file bindings after independent loading.
            packages.clear();
            if (!capturePackages(root, project.savedFileBytes, packages, diagnostic))
                return {};
            if (!ProjectPackageSnapshot::equal(project.packages, packages)) {
                fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-BINDING-CONFLICT");
                return {};
            }
        } else if (!noProjectContext(root, diagnostic))
            return {};
        // Cold script domains require one explicit session retaining the exact
        // independently loaded owners, rather than unrelated provider callbacks.
        const auto services = core::makeShared<EditorSceneServiceSession>(
            candidate->resources.retainManager(), candidate->resources.physicsWorld2D());
        const auto document = [&] {
            auto result = core::makeUnique<EditorSceneDocument>(false);
            if (!result->bindServiceSession(services, diagnostic))
                return core::UniquePtr<EditorSceneDocument>{};
            return result;
        };
        const auto restore = [&](std::string_view yaml, EditorSceneDocument& target) {
            return EditorSceneFileFormat::restore(
                yaml, target, root, snapshot.project ? &draft : nullptr, diagnostic);
        };
        core::pmr::Vector<std::filesystem::path> boundPaths;
        if (!snapshot.scene.path.empty()) {
            std::filesystem::path path, bound;
            if (!resolve(root, snapshot.scene.path, ProjectPathIntent::NewFile, path, diagnostic)
                || !resolve(root,
                    snapshot.scene.boundTarget,
                    ProjectPathIntent::NewFile,
                    bound,
                    diagnostic))
                return {};
            if (path != bound) {
                fail(diagnostic, "EDITOR-RECOVERY-SCENE-TARGET-CHANGED");
                return {};
            }
            auto baseline = document();
            if (!baseline)
                return {};
            core::pmr::String hash;
            if (!restore(snapshot.scene.savedFileBytes, *baseline)
                || !EditorSceneFileFormat::observeHash(*baseline,
                    EditorSceneFileFormat::version(snapshot.scene.savedFileBytes),
                    root,
                    snapshot.project ? &draft : nullptr,
                    hash,
                    diagnostic))
                return {};
            if (hash != snapshot.scene.savedContentHash) {
                fail(diagnostic, "EDITOR-RECOVERY-SCENE-SAVEPOINT-MISMATCH");
                return {};
            }
            if (!fileBaseline(path,
                    snapshot.scene.path,
                    snapshot.scene.savedFileBytes,
                    EditorSceneAuthoringCodec::maxDocumentBytes,
                    *candidate,
                    diagnostic))
                return {};
            candidate->sceneTarget = path;
            boundPaths.push_back(path);
        }
        candidate->document = document();
        if (!candidate->document)
            return {};
        if (!restore(snapshot.scene.yaml, *candidate->document))
            return {};
        for (const auto& source : snapshot.scripts) {
            std::filesystem::path path;
            if (!resolve(root, source.path, ProjectPathIntent::NewFile, path, diagnostic))
                return {};
            for (const auto& previous : boundPaths) {
                std::error_code error;
                if (previous == path || std::filesystem::equivalent(previous, path, error)) {
                    fail(diagnostic, "EDITOR-RECOVERY-DOCUMENT-PATH-ALIAS");
                    return {};
                }
            }
            boundPaths.push_back(path);
            if (!fileBaseline(path,
                    source.path,
                    source.savedFileBytes,
                    ScriptDocumentSession::maxScriptBytes,
                    *candidate,
                    diagnostic))
                return {};
            ScriptDocumentSession::Document script;
            script.path = path;
            script.text = source.text;
            script.savedText = source.savedText;
            script.savedFileBytes = source.savedFileBytes;
            script.newline = source.newline;
            script.cursor = source.cursor;
            script.anchor = source.anchor;
            script.scrollLine = source.scrollLine;
            candidate->scripts.push_back(std::move(script));
        }
        if (!validateFiles(*candidate, root, diagnostic))
            return {};
        diagnostic.clear();
        return candidate;
    } catch (...) {
        fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-PREPARATION-FAILED");
        return {};
    }
}

bool EditorRecoveryPreparation::candidateFingerprint(const Candidate& candidate,
    EditorRecoveryBudget budget,
    core::ByteBuffer& destination,
    core::pmr::String& diagnostic)
{
    if (!candidate.document || candidate.scripts.size() > budget.maxScripts
        || candidate.scripts.size() != candidate.submitted.scripts.size()
        || candidate.files.size() > budget.maxScripts + 1u
        || candidate.conflicts.size() > candidate.files.size())
        return fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-STRUCTURE");
    detail::RecoveryDigest digest(EditorRecoveryAdmissionBudget{}.maxObservedBytes);
    digest.field("arp.editor-recovery-candidate-observation/1");
    {
        core::pmr::String encoded;
        if (!EditorRecoveryCodec::encode(candidate.submitted, encoded, diagnostic, budget))
            return false;
        digest.field(encoded);
    }
    {
        core::pmr::String yaml;
        ProjectManifest project;
        ProjectDiagnostic issue;
        if (candidate.submitted.project
            && !ProjectManifestCodec::decode(
                candidate.submitted.project->draftYaml, project, issue))
            return fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-PROJECT");
        if (!EditorSceneFileFormat::observe(*candidate.document,
                EditorSceneFileFormat::version(candidate.submitted.scene.yaml),
                candidate.root,
                candidate.submitted.project ? &project : nullptr,
                yaml,
                diagnostic))
            return false;
        digest.field(yaml);
    }
    digest.field(core::FilePath::utf8(candidate.root));
    digest.field(core::FilePath::utf8(candidate.sceneTarget));
    digest.number(candidate.scripts.size());
    for (const auto& script : candidate.scripts) {
        if (script.id || !script.undo.empty() || !script.redo.empty()
            || script.text.size() > ScriptDocumentSession::maxScriptBytes
            || script.savedText.size() > ScriptDocumentSession::maxScriptBytes
            || script.savedFileBytes.size() > ScriptDocumentSession::maxScriptBytes)
            return fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-SCRIPT-STATE");
        digest.field(core::FilePath::utf8(script.path));
        digest.field(script.text);
        digest.field(script.savedText);
        digest.field(script.savedFileBytes);
        digest.field(script.newline);
        digest.number(script.cursor);
        digest.number(script.anchor);
        digest.number(script.scrollLine);
    }
    digest.number(candidate.files.size());
    for (const auto& file : candidate.files) {
        digest.field(file.relativePath);
        digest.field(core::FilePath::utf8(file.target));
        digest.number(file.bytes.has_value());
        if (file.bytes)
            digest.field(*file.bytes);
    }
    digest.number(candidate.conflicts.size());
    for (const auto& conflict : candidate.conflicts) {
        digest.field(conflict.relativePath);
        digest.number(static_cast<std::uint64_t>(conflict.kind));
    }
    // Provenance is observable here; complete in-place resource inventory and
    // writer quiescence remain separate, currently unavailable publication gates.
    digest.field(candidate.resources.manifestSnapshot());
    digest.field(candidate.resources.metadataSnapshot());
    digest.field(candidate.resources.packageProvenanceDiagnostic());
    const auto& inputs = candidate.resources.inputBindings();
    digest.number(inputs.size());
    for (const auto& input : inputs) {
        digest.field(input.path);
        digest.number(input.byteCount);
        digest.field(input.fingerprint);
    }
    const auto& packages = candidate.resources.packageBindings();
    digest.number(packages.size());
    for (const auto& package : packages) {
        digest.field(package.directory);
        digest.field(package.fingerprint);
    }
    if (!digest.valid())
        return fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-BYTE-BUDGET");
    destination = digest.finish();
    diagnostic.clear();
    return true;
}

core::UniquePtr<EditorRecoveryPreparation::WorkspaceState> EditorRecoveryPreparation::prepareState(
    const Candidate& candidate, core::pmr::String& diagnostic)
{
    auto result = core::makeUnique<WorkspaceState>();
    const auto& snapshot = candidate.submitted;
    if (snapshot.project) {
        ProjectManifest saved, draft;
        ProjectDiagnostic issue;
        core::pmr::String savedYaml, draftYaml;
        if (!ProjectManifestCodec::decode(snapshot.project->savedFileBytes, saved, issue)
            || !ProjectManifestCodec::decode(snapshot.project->draftYaml, draft, issue)
            || !ProjectManifestCodec::encode(saved, savedYaml, issue)
            || !ProjectManifestCodec::encode(draft, draftYaml, issue)) {
            diagnostic = "EDITOR-RECOVERY-PREPARED-PROJECT-INVALID";
            return {};
        }
        result->projectDirty_ = savedYaml != draftYaml;
        result->projectSavedBytes_ = snapshot.project->savedFileBytes;
        result->projectDraft_ = std::move(draft);
    }
    result->sceneAuthoringVersion_ = EditorSceneFileFormat::version(snapshot.scene.yaml);
    result->scenePath_ = snapshot.scene.path;
    result->sceneTarget_ = candidate.sceneTarget;
    result->sceneSavedHash_ = snapshot.scene.savedContentHash;
    result->sceneSavedBytes_.assign(
        snapshot.scene.savedFileBytes.begin(), snapshot.scene.savedFileBytes.end());
    result->activeScriptIndex_ =
        snapshot.activeScript.value_or(ScriptDocumentSession::noActiveDocument);
    result->workspace_ = snapshot.workspace;
    diagnostic.clear();
    return result;
}

core::UniquePtr<EditorRecoveryPreparation::WorkspaceCandidate>
EditorRecoveryPreparation::prepareForWorkspace(const EditorRecoverySnapshot& snapshot,
    const EditorShell& shell,
    bool discardChanges,
    core::pmr::String& diagnostic,
    EditorRecoveryBudget budget,
    EditorRecoveryAdmissionBudget admissionBudget)
{
    try {
        auto candidate = core::makeUnique<WorkspaceCandidate>();
        candidate->admission_ =
            EditorRecoveryAdmission::capture(shell, discardChanges, diagnostic, admissionBudget);
        if (!candidate->admission_)
            return {};
        candidate->recovery = prepare(snapshot, shell.projectRoot(), diagnostic, budget);
        if (!candidate->recovery)
            return {};
        candidate->state_ = prepareState(*candidate->recovery, diagnostic);
        if (!candidate->state_)
            return {};
        candidate->budget_ = budget;
        candidate->scene_ = candidate->recovery->document->ownerTicket();
        candidate->resources_ = candidate->recovery->resources.retainManager();
        const auto physics = candidate->recovery->resources.physicsWorld2D();
        candidate->physics_ = physics;
        candidate->hasPhysics_ = bool(physics);
        if (!candidateFingerprint(*candidate->recovery, budget, candidate->fingerprint_, diagnostic)
            || !validateForWorkspace(*candidate, shell, diagnostic))
            return {};
        return candidate;
    } catch (...) {
        fail(diagnostic, "EDITOR-RECOVERY-WORKSPACE-PREPARATION-FAILED");
        return {};
    }
}

bool EditorRecoveryPreparation::validateForWorkspace(
    const WorkspaceCandidate& candidate, const EditorShell& shell, core::pmr::String& diagnostic)
{
    try {
        if (!candidate.admission_ || !candidate.state_ || !candidate.recovery
            || !candidate.recovery->document || candidate.fingerprint_.empty())
            return fail(diagnostic, "EDITOR-RECOVERY-WORKSPACE-CANDIDATE-INCOMPLETE");
        const auto& recovery = *candidate.recovery;
        const auto resources = candidate.resources_.lock();
        const auto physics = candidate.physics_.lock();
        if (!candidate.scene_.sameCurrentEpoch(recovery.document->ownerTicket()) || !resources
            || resources != recovery.resources.retainManager()
            || (candidate.hasPhysics_ && !physics)
            || physics != recovery.resources.physicsWorld2D())
            return fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-OWNER-CHANGED");
        if (!EditorRecoveryAdmission::validate(shell, *candidate.admission_, diagnostic))
            return false;
        core::ByteBuffer current;
        if (!candidateFingerprint(recovery, candidate.budget_, current, diagnostic))
            return false;
        if (current != candidate.fingerprint_)
            return fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-CONTENT-CHANGED");
        return validateFiles(recovery, shell.projectRoot(), diagnostic);
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-CANDIDATE-OBSERVATION-FAILED");
    }
}

} // namespace arp::editor

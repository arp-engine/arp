#include "editor/workspace/editor_project_source_save_task.h"

#include "core/text/text_decoder.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_scene_script_bindings.h"
#include "scene/scene_asset_resource.h"

#include <algorithm>

namespace arp::editor {
core::UniquePtr<EditorProjectSourceSaveTask> EditorProjectSourceSaveTask::prepare(
    ProjectSourceContext context,
    ProjectInputBudget budget,
    EditorProjectResources& resources,
    ProjectBrowserModel& browser,
    EditorSceneDocument& document,
    const ProjectManifest& settings,
    const char*& diagnostic) noexcept
{
    diagnostic = "EDITOR-PROJECT-SAVE-SOURCE-OWNER";
    try {
        const char* issue = nullptr;
        if (!context.validate(issue)) {
            diagnostic = issue;
            return {};
        }
        if (!resources.manifest_ || resources.sourceSnapshot_ != context.snapshot
            || !browser.sourceSnapshot() || browser.sourceSnapshot()->project() != context.snapshot)
            return {};
        auto candidate = core::makeUnique<EditorProjectSourceSaveTask>();
        candidate->context_ = std::move(context);
        candidate->settings_ = settings;
        candidate->baselineYaml_ = resources.manifestSnapshot_;
        ProjectDiagnostic codec;
        if (!ProjectManifestCodec::encode(settings, candidate->submittedYaml_, codec)) {
            diagnostic = codec.code;
            return {};
        }
        if (!ProjectManifestCodec::isSettingsOnlyChange(*resources.manifest_, settings)) {
            diagnostic = "EDITOR-PROJECT-SAVE-DEPENDENCY-CHANGE";
            return {};
        }
        if (!settings.entryScene.empty()
            && !resources.manager().find<scene::SceneAssetResource>(settings.entryScene).ready()) {
            diagnostic = "EDITOR-PROJECT-ENTRY-SCENE-NOT-READY";
            return {};
        }
        const auto& source = *candidate->context_.snapshot;
        const auto* manifest = source.find("project.arp.yaml");
        if (!manifest) {
            diagnostic = "EDITOR-PROJECT-SAVE-MANIFEST-NOT-ACQUIRED";
            return {};
        }
        const auto& raw = manifest->receipt.bytes;
        const auto decoded = core::TextDecoder::decode(
            {raw.empty() ? "" : reinterpret_cast<const char*>(raw.data()), raw.size()},
            {1,
                core::TextEncoding::Utf8,
                core::TextBomPolicy::Strip,
                core::TextNulPolicy::Reject,
                EditorProjectResources::maxManifestBytes});
        if (decoded.status != core::TextDecodeStatus::Success
            || decoded.text != resources.manifestSnapshot_) {
            diagnostic = "EDITOR-PROJECT-SAVE-MANIFEST-BASELINE";
            return {};
        }
        candidate->resources_ = &resources;
        candidate->document_ = &document;
        candidate->manager_ = resources.manager_;
        candidate->physics_ = resources.physicsWorld2D_;
        candidate->documentEpoch_ = document.ownerTicket();
        candidate->scripts_ = document.scriptBindings_;
        candidate->services_ = document.sceneServiceSession();
        if (!candidate->documentEpoch_.isCurrent() || !candidate->services_
            || candidate->services_->resources() != candidate->manager_.get()
            || candidate->services_->physics() != candidate->physics_) {
            diagnostic = "EDITOR-PROJECT-SAVE-DOCUMENT-STALE";
            return {};
        }
        candidate->serviceEpoch_ = candidate->services_->ownerTicket();
        if (candidate->scripts_) {
            const auto& scripts = *candidate->scripts_;
            core::pmr::String scriptIssue;
            if (!scripts.sources_ || scripts.sources_->snapshot != candidate->context_.snapshot
                || scripts.sources_->files != candidate->context_.files
                || scripts.sources_->epoch != candidate->context_.epoch
                || !scripts.matches(
                    document.scene(), document.sceneServiceSession(), document.scriptSceneRegistry_)
                || !scripts.catalog_->modules().matchesManifest(settings)
                || !scripts.verifySources(scriptIssue)) {
                diagnostic = "EDITOR-PROJECT-SAVE-SCRIPT-BASELINE";
                return {};
            }
        }
        const auto maxBytes =
            std::min(budget.maxFileBytes, EditorProjectResources::maxManifestBytes);
        candidate->prepared_ = source.prepareScopeFileSave(*candidate->context_.files,
            candidate->context_.epoch,
            "project.arp.yaml",
            {candidate->submittedYaml_.begin(), candidate->submittedYaml_.end()},
            maxBytes,
            diagnostic);
        if (!candidate->prepared_)
            return {};
        candidate->browser_ = browser.prepareProjectRebind(*candidate->prepared_, diagnostic);
        if (!candidate->browser_)
            return {};
        candidate->verification_ =
            core::makeUnique<ProjectSourceVerification>(candidate->context_, budget);
        candidate->operation_ = core::makeUnique<ProjectFileSaveTask>(
            *candidate->context_.files, candidate->prepared_->takeRequest());
        diagnostic = nullptr;
        return candidate;
    } catch (...) {
        diagnostic = "EDITOR-PROJECT-SAVE-PREPARATION";
        return {};
    }
}

EditorProjectSourceSaveTask::~EditorProjectSourceSaveTask()
{
    cancel();
}

void EditorProjectSourceSaveTask::cancel() noexcept
{
    if (operation_ && !operation_->terminal())
        operation_->cancel();
    if (verification_)
        verification_->cancel();
}

void EditorProjectSourceSaveTask::advance() noexcept
{
    if (!operation_ || operation_->terminal())
        return;
    if (!operation_->submitted() && !operation_->cancellationRequested()) {
        verification_->advance();
        const auto state = verification_->state();
        if (state == ProjectSourceVerificationState::Pending)
            return;
        if (state != ProjectSourceVerificationState::Verified) {
            operation_->reject(core::io::FileIoStatus::InvalidRequest, verification_->diagnostic());
            return;
        }
    }
    operation_->advance();
}

void EditorProjectSourceSaveTask::reject(
    core::io::FileIoStatus status, const char* diagnostic) noexcept
{
    if (operation_)
        operation_->reject(status, diagnostic);
}

ProjectFileReceiptObservation EditorProjectSourceSaveTask::receiptObservation() const noexcept
{
    return operation_ ? operation_->receiptObservation() : ProjectFileReceiptObservation{};
}

bool EditorProjectSourceSaveTask::ownersCurrent(
    const EditorProjectResources& resources, const EditorSceneDocument& document) const noexcept
{
    return resources_ == &resources && document_ == &document
        && resources.sourceSnapshot_ == context_.snapshot && resources.manager_ == manager_
        && resources.physicsWorld2D_ == physics_ && resources.manifest_
        && resources.manifestSnapshot_ == baselineYaml_
        && documentEpoch_.sameCurrentEpoch(document.ownerTicket())
        && services_ == document.sceneServiceSession() && services_
        && serviceEpoch_.sameCurrentEpoch(services_->ownerTicket())
        && services_->resources() == manager_.get() && services_->physics() == physics_
        && document.scriptBindings_ == scripts_
        && (!scripts_
            || (scripts_->sources_ && scripts_->sources_->snapshot == context_.snapshot
                && scripts_->matches(document.scene(),
                    document.sceneServiceSession(),
                    document.scriptSceneRegistry_)));
}

core::SharedPtr<const ProjectSourceSnapshot> EditorProjectSourceSaveTask::publish(
    EditorProjectResources& resources,
    ProjectBrowserModel& browser,
    EditorSceneDocument& document) noexcept
{
    diagnostic_ = "EDITOR-PROJECT-SAVE-PUBLICATION-REFUSED";
    if (published_ || !terminal() || cancelled() || !prepared_ || !browser_
        || !ownersCurrent(resources, document))
        return {};
    auto committed = prepared_->finish(*context_.files, context_.epoch, operation_->result());
    if (!committed) {
        diagnostic_ = prepared_->diagnostic();
        return {};
    }
    // All owner/recipient checks precede the first mutation. Nothing below
    // allocates, invokes scripts, reloads a resource or changes a Scene object.
    if (!browser_->canFinish(*prepared_, committed, browser))
        return {};
    if (!browser_->finish(*prepared_, committed, browser))
        return {};
    resources.sourceSnapshot_ = committed;
    resources.manifest_->name.swap(settings_.name);
    resources.manifest_->entryScene.swap(settings_.entryScene);
    resources.manifestSnapshot_.swap(submittedYaml_);
    if (scripts_)
        scripts_->sources_->snapshot = committed;
    published_ = true;
    diagnostic_ = nullptr;
    return committed;
}

void EditorProjectSourceSaveTask::releasePreparation() noexcept
{
    if (!terminal())
        return;
    verification_.reset();
    prepared_.reset();
    browser_.reset();
    context_.snapshot.reset();
    manager_.reset();
    physics_.reset();
    scripts_.reset();
    services_.reset();
    core::pmr::String{}.swap(submittedYaml_);
    core::pmr::String{}.swap(baselineYaml_);
    settings_ = ProjectManifest{};
}

bool EditorProjectSourceSaveTask::submitted() const noexcept
{
    return operation_ && operation_->submitted();
}

bool EditorProjectSourceSaveTask::terminal() const noexcept
{
    return operation_ && operation_->terminal();
}

bool EditorProjectSourceSaveTask::cancelled() const noexcept
{
    return operation_ && operation_->cancellationRequested();
}

const core::io::FileSaveResult& EditorProjectSourceSaveTask::result() const noexcept
{
    return operation_->result();
}
} // namespace arp::editor

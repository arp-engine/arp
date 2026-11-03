#pragma once

#include "core/ownership/lifetime_epoch.h"
#include "editor/project/project_file_save_task.h"
#include "editor/project/project_source_verification.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/project_browser_model.h"

namespace arp::editor {
class EditorSceneDocument;
class EditorSceneScriptBindings;
class EditorSceneServiceSession;

// One owner-thread project settings transaction. IO callbacks retain only the
// shared operation/source verifier's private state, never these live owners.
class EditorProjectSourceSaveTask final {
public:
    EditorProjectSourceSaveTask() = default;
    ~EditorProjectSourceSaveTask();
    static core::UniquePtr<EditorProjectSourceSaveTask> prepare(ProjectSourceContext,
        ProjectInputBudget,
        EditorProjectResources&,
        ProjectBrowserModel&,
        EditorSceneDocument&,
        const ProjectManifest&,
        const char*& diagnostic) noexcept;
    void advance() noexcept;
    void cancel() noexcept;
    void reject(core::io::FileIoStatus, const char*) noexcept;
    void releasePreparation() noexcept;
    bool submitted() const noexcept;
    bool terminal() const noexcept;
    bool cancelled() const noexcept;

    bool published() const noexcept { return published_; }

    const core::io::FileSaveResult& result() const noexcept;
    ProjectFileReceiptObservation receiptObservation() const noexcept;

    const ProjectSourceContext& context() const noexcept { return context_; }

    const ProjectManifest& settings() const noexcept { return settings_; }

    bool ownersCurrent(const EditorProjectResources&, const EditorSceneDocument&) const noexcept;
    core::SharedPtr<const ProjectSourceSnapshot> publish(
        EditorProjectResources&, ProjectBrowserModel&, EditorSceneDocument&) noexcept;

    const char* diagnostic() const noexcept { return diagnostic_; }

private:
    ProjectSourceContext context_;
    core::UniquePtr<ProjectSourceVerification> verification_;
    core::UniquePtr<ProjectSourceSnapshot::PreparedFileSave> prepared_;
    core::UniquePtr<ProjectBrowserModel::PreparedProjectRebind> browser_;
    core::UniquePtr<ProjectFileSaveTask> operation_;
    ProjectManifest settings_;
    core::pmr::String submittedYaml_, baselineYaml_;
    const EditorProjectResources* resources_ = nullptr;
    const EditorSceneDocument* document_ = nullptr;
    core::SharedPtr<resource::ResourceManager> manager_;
    core::SharedPtr<physics2d::PhysicsWorld2D> physics_;
    core::SharedPtr<EditorSceneScriptBindings> scripts_;
    core::SharedPtr<EditorSceneServiceSession> services_;
    core::LifetimeEpoch::Ticket documentEpoch_;
    core::LifetimeEpoch::Ticket serviceEpoch_;
    bool published_ = false;
    const char* diagnostic_ = nullptr;
};
} // namespace arp::editor

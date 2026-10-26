#include "editor/workspace/editor_scene_export.h"

#include "core/io/filesystem_path.h"

namespace arp::editor {

struct EditorSceneExport::Task {
    std::filesystem::path root;
    ProjectSceneAuthoring::Snapshot snapshot;
    SceneExportResult result;
    // All registration happens on the owner thread. Package inspection only
    // invokes the built-in value schemas/validators, never these constructors.
    // Even unused constructor captures refer to this job's owners, not the UI.
    resource::ResourceManager resources;
    core::SharedPtr<physics2d::PhysicsWorld2D> world;
    core::SharedPtr<scene::SceneComponentFactory> factory;
    bool completed = false;

    static core::runtime::WorkStatus execute(void* context)
    {
        auto& task = *static_cast<Task*>(context);
        task.result.status = SceneExportStatus::Failed;
        try {
            std::filesystem::path output;
            ProjectDiagnostic diagnostic;
            // Recheck containment, parent and exclusive output immediately
            // before work. StagedDirectory also enforces exclusive publication.
            if (!ProjectManifestCodec::resolvePath(task.root,
                    task.result.relativePath,
                    ProjectPathIntent::NewDirectory,
                    output,
                    diagnostic)
                || core::FilePath::utf8(output) != task.result.outputPath) {
                task.result.diagnostic = "EDITOR-SCENE-EXPORT-PATH-CHANGED";
                return core::runtime::WorkStatus::Failed;
            }
            if (!ProjectSceneAuthoring::exportSnapshot(
                    task.snapshot, *task.factory, output, task.result.diagnostic))
                return core::runtime::WorkStatus::Failed;
            task.result.status = SceneExportStatus::ExportedUnregistered;
            return core::runtime::WorkStatus::Completed;
        } catch (...) {
            task.result.diagnostic = "EDITOR-SCENE-EXPORT-PREPARATION-FAILED";
            return core::runtime::WorkStatus::Failed;
        }
    }

    static void complete(void* context, core::runtime::WorkResult)
    {
        static_cast<Task*>(context)->completed = true;
    }
};

// One submitted job, one worker, one queued completion and one callback per poll.
EditorSceneExport::EditorSceneExport() : runtime_({1u, 1u, 1u, 1u})
{}

EditorSceneExport::~EditorSceneExport()
{
    runtime_.shutdown(core::runtime::AsyncRuntimeShutdownPolicy::Drain);
}

bool EditorSceneExport::start(const std::filesystem::path& root,
    const ProjectManifest& manifest,
    std::string_view relativeDirectory,
    std::uint64_t documentEpoch,
    std::string_view authoringHash,
    ProjectSceneAuthoring::Snapshot snapshot,
    core::pmr::String& diagnostic,
    SceneExportResult* acceptance)
{
    if (pending()) {
        diagnostic = "EDITOR-SCENE-EXPORT-BUSY";
        return false;
    }
    if (relativeDirectory.size() > maxRelativeDirectoryBytes) {
        diagnostic = "EDITOR-SCENE-EXPORT-PATH-BUDGET";
        return false;
    }
    ProjectDiagnostic pathDiagnostic;
    std::filesystem::path output;
    if (!ProjectManifestCodec::resolvePath(
            root, relativeDirectory, ProjectPathIntent::NewDirectory, output, pathDiagnostic)) {
        diagnostic = pathDiagnostic.code;
        diagnostic += ": ";
        diagnostic += pathDiagnostic.field;
        return false;
    }
    auto candidate = core::makeUnique<Task>();
    std::error_code error;
    candidate->root = std::filesystem::canonical(root, error);
    if (error) {
        diagnostic = "EDITOR-SCENE-EXPORT-ROOT-INVALID";
        return false;
    }
    if (manifest.physics2d) {
        candidate->world = core::makeShared<physics2d::PhysicsWorld2D>(*manifest.physics2d);
        if (!candidate->world->valid()) {
            diagnostic = "EDITOR-SCENE-EXPORT-PHYSICS-CONFIG";
            return false;
        }
    }
    candidate->factory =
        ProjectSceneAuthoring::factory(candidate->resources, candidate->world, diagnostic);
    if (!candidate->factory)
        return false;
    candidate->snapshot = std::move(snapshot);
    candidate->result.taskId = nextTaskId_;
    candidate->result.status = SceneExportStatus::Pending;
    candidate->result.documentEpoch = documentEpoch;
    candidate->result.projectId = manifest.id;
    candidate->result.resourceId = candidate->snapshot.resourceId;
    candidate->result.contentHash = candidate->snapshot.contentHash;
    candidate->result.authoringHash = authoringHash;
    candidate->result.relativePath = relativeDirectory;
    candidate->result.outputPath = core::FilePath::utf8(output);
    // Prepare the public copy before submission: worker may start immediately.
    auto pendingResult = candidate->result;
    // A caller's unpublished receipt must own its bytes before IO can commit.
    if (acceptance)
        *acceptance = pendingResult;
    task_ = std::move(candidate);
    const auto submitted = runtime_.workService().submit(
        {Task::execute, task_.get(), Task::complete, task_.get(), {}});
    if (!submitted.accepted()) {
        task_.reset();
        diagnostic = "EDITOR-SCENE-EXPORT-SUBMISSION-FAILED";
        return false;
    }
    result_ = std::move(pendingResult);
    ++nextTaskId_;
    diagnostic.clear();
    return true;
}

bool EditorSceneExport::ready()
{
    runtime_.dispatch();
    return task_ && task_->completed;
}

bool EditorSceneExport::finish()
{
    if (!ready())
        return false;
    result_ = std::move(task_->result);
    task_.reset();
    return true;
}

} // namespace arp::editor

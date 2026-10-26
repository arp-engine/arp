#pragma once

#include "core/runtime/async_runtime.h"
#include "editor/project/project_scene_authoring.h"

namespace arp::editor {

enum class SceneExportStatus : std::uint8_t { Unknown, Pending, ExportedUnregistered, Failed };

struct SceneExportResult {
    std::uint64_t taskId = 0;
    SceneExportStatus status = SceneExportStatus::Unknown;
    std::uint64_t documentEpoch = 0;
    core::pmr::String projectId;
    core::pmr::String resourceId;
    core::pmr::String contentHash;
    core::pmr::String authoringHash;
    core::pmr::String relativePath;
    core::pmr::String outputPath;
    core::pmr::String diagnostic;
};

// Single cold job, with one retained result. A newly accepted job retires the
// previous result. Destruction drains publication; published directories belong
// to the caller and are never rolled back when the workspace closes.
class EditorSceneExport final {
public:
    static constexpr std::size_t maxRelativeDirectoryBytes = 512;
    EditorSceneExport();
    ~EditorSceneExport();

    bool start(const std::filesystem::path& root,
        const ProjectManifest&,
        std::string_view relativeDirectory,
        std::uint64_t documentEpoch,
        std::string_view authoringHash,
        ProjectSceneAuthoring::Snapshot snapshot,
        core::pmr::String& diagnostic,
        SceneExportResult* acceptance = nullptr);
    bool ready();
    bool finish();

    bool pending() const noexcept { return bool(task_); }

    const SceneExportResult& result() const noexcept { return result_; }

private:
    struct Task;
    core::UniquePtr<Task> task_;
    core::runtime::AsyncRuntime runtime_;
    SceneExportResult result_;
    std::uint64_t nextTaskId_ = 1;
};

} // namespace arp::editor

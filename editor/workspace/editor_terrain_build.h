#pragma once

#include "core/runtime/async_runtime.h"
#include "editor/workspace/editor_project_resources.h"

#include <optional>

namespace arp::editor {

// One bounded preparation task. The worker builds a candidate resource owner;
// the command thread alone commits the project manifest and publishes it.
class EditorTerrainBuild final {
public:
    struct Result {
        bool success = false;
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        core::UniquePtr<EditorProjectResources> resources;
    };

    EditorTerrainBuild();
    ~EditorTerrainBuild();
    bool start(const std::filesystem::path& projectRoot,
        std::string_view authoringPath,
        std::string_view packagePath,
        std::optional<std::string_view> expectedManifest = std::nullopt);
    bool ready();
    Result finish(const std::filesystem::path& currentProjectRoot);

    bool pending() const noexcept { return bool(task_); }

private:
    struct Task;
    core::UniquePtr<Task> task_;
    core::runtime::AsyncRuntime runtime_;
};
} // namespace arp::editor

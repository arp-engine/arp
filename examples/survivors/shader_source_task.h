#pragma once

#include "../common/startup_resource_task.h"
#include "renderer/shader_package_reflection.h"

namespace arp::survivors {

// Explicit compiler publication and cold-start limits for the existing sprite
// program. These are source budgets, not inferred graphics capabilities.
struct SceneShaderSources {
    static constexpr const char* publication = "scene_2d_sprites.artifact-set.yaml";
    static constexpr const char* module = "scene_2d_sprites.slang";
    static constexpr std::size_t maxFileBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maxSourceBytes = 16 * 1024 * 1024;
    static constexpr std::size_t maxFiles = 6;
};

class ShaderSourceTask final {
public:
    ShaderSourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        core::PoolHandle,
        core::runtime::CancellationToken = {});

    void advance() noexcept { task_.advance(); }

    void cancel() noexcept { task_.cancel(); }

    examples::StartupResourcePhase phase() const noexcept { return task_.phase(); }

    std::string_view diagnostic() const noexcept { return task_.diagnostic(); }

    std::uint64_t readCount() const noexcept { return task_.readCount(); }

    renderer::ShaderPackagePreparationDesc preparation() const;

private:
    examples::StartupResourceTask task_;
};
} // namespace arp::survivors

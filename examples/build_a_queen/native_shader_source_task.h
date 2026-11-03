#pragma once

#include "shader/generated_shader_artifact_set.h"
#include "startup_resource_task.h"

#include <array>

namespace arp::build_a_queen {

// The native product's explicit compiler publications, in renderer binding order.
struct NativeShaderSources {
    static constexpr std::array<const char*, 6> publications{"queen_scene.artifact-set.yaml",
        "queen_sky.artifact-set.yaml",
        "queen_skinned.artifact-set.yaml",
        "queen_skinned_shadow.artifact-set.yaml",
        "queen_shadow.artifact-set.yaml",
        "gui_overlay.artifact-set.yaml"};
    static constexpr std::size_t maxFileBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maxFiles = 64;
    static constexpr std::size_t maxSourceBytes = 32 * 1024 * 1024;
    static constexpr std::size_t maxDecodedBytes = 64 * 1024 * 1024;
    std::array<core::SharedPtr<const shader::GeneratedShaderArtifactSet>, 6> programs;
    bool valid() const noexcept;
};

class NativeShaderSourceTask final {
public:
    NativeShaderSourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        core::PoolHandle,
        core::runtime::CancellationToken = {});
    void advance() noexcept;
    void cancel() noexcept;
    StartupResourcePhase phase() const noexcept;
    std::string_view diagnostic() const noexcept;
    std::uint64_t readCount() const noexcept;
    NativeShaderSources sources() const;

private:
    StartupResourceTask task_;
};
} // namespace arp::build_a_queen

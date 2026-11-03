#pragma once

#include "startup_resource_task.h"

#include <array>
#include <string_view>

namespace arp::shader {
class ShaderArtifactSource;
}

namespace arp::build_a_queen {

using WebShaderSourcePhase = StartupResourcePhase;

// Startup-only example coordinator. ResourceGraphExecutor owns acquisition and
// the shader resource owns dependency discovery; this task never parses YAML.
// The borrowed source is available only when Ready and lives with this task.
class WebShaderSourceTask final {
public:
    static constexpr std::array<const char*, 4> publications{
        "queen_portable_sky.webgl-program.yaml",
        "queen_portable_shadow.webgl-program.yaml",
        "queen_portable.webgl-program.yaml",
        "gui_bitmap.webgl-program.yaml"};
    static constexpr std::size_t maxFiles = 128;
    static constexpr std::size_t maxFileBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maxSourceBytes = 16 * 1024 * 1024;
    static constexpr std::size_t maxMetadataBytes = 32 * 1024 * 1024;

    WebShaderSourceTask(core::io::FileService&,
        core::runtime::CompletionQueue&,
        core::PoolHandle,
        core::runtime::CancellationToken cancellation = {});
    ~WebShaderSourceTask();
    WebShaderSourceTask(const WebShaderSourceTask&) = delete;
    WebShaderSourceTask& operator=(const WebShaderSourceTask&) = delete;

    void advance() noexcept;
    void cancel() noexcept;
    WebShaderSourcePhase phase() const noexcept;
    std::string_view diagnostic() const noexcept;
    const shader::ShaderArtifactSource* source() const noexcept;
    std::size_t fileCount() const noexcept;
    std::uint64_t readCount() const noexcept;
    std::uint64_t bytesRead() const noexcept;

private:
    core::UniquePtr<StartupResourceTask> task_;
};
} // namespace arp::build_a_queen

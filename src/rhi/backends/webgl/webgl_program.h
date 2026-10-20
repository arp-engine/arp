#pragma once
#include "rhi/backends/webgl/webgl_resources.h"
#include "rhi/semantics/webgl_program_creation.h"
#include "shader/webgl_shader_program_artifact.h"

namespace arp::rhi::webgl {
class WebGlProgram final {
    class ConstructionKey {
        friend class WebGlResources;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Impl;
    WebGlProgram(ConstructionKey, core::UniquePtr<Impl>);
    ~WebGlProgram();
    WebGlProgram(const WebGlProgram&) = delete;
    WebGlProgram& operator=(const WebGlProgram&) = delete;
    GpuResourceId id() const noexcept;
    const char* currentDiagnostic() const noexcept;
    const std::shared_ptr<const shader::WebGlShaderProgramArtifact>& artifact() const noexcept;

private:
    friend class WebGlResources;
    friend class WebGlDevice;
    friend class WebGlExecution;
    friend class WebGlRenderPipeline;
    core::UniquePtr<Impl> impl_;
};
} // namespace arp::rhi::webgl

#pragma once
#include "rhi/backends/webgl/webgl_program.h"
#include "rhi/backends/webgl/webgl_resources_internal.h"

namespace arp::rhi::webgl {
struct WebGlProgram::Impl {
    struct Uniform {
        explicit Uniform(std::string_view n) : name(n, &core::defaultCpuMemoryResource()) {}

        core::pmr::String name;
        const shader::WebGlUniformBlock* block = nullptr;
        const shader::WebGlUniformLeaf* leaf = nullptr;
        std::uint32_t type = 0;
        int location = -1;
        bool required = true, active = false;
        std::uint32_t textureUnit = UINT32_MAX;
    };

    std::shared_ptr<WebGlResourcePayload> object;
    std::shared_ptr<const shader::WebGlShaderProgramArtifact> artifact;
    core::pmr::Vector<Uniform> uniforms{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<int> attributes{&core::defaultCpuMemoryResource()};
};
} // namespace arp::rhi::webgl

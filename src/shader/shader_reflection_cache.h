#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "shader/shader_compiler.h"

#include <string>

namespace arp::shader {

struct ShaderReflectionModule {
    core::pmr::String moduleName{&core::defaultCpuMemoryResource()};
    core::pmr::String sourcePath{&core::defaultCpuMemoryResource()};
    core::pmr::String spirvPath{&core::defaultCpuMemoryResource()};
    core::pmr::String metalPath{&core::defaultCpuMemoryResource()};
    core::pmr::String wgslPath{&core::defaultCpuMemoryResource()};
    ShaderReflection reflection;
};

struct ShaderReflectionCacheLoadResult {
    bool success = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    ShaderReflectionModule module;
};

// Loads per-shader reflection metadata from a YAML document (the canonical
// format produced by scripts/compile_shaders.py, replacing the legacy JSON).
// The YAML is parsed with fkYAML (3rd/fkYAML), giving structured access to
// nested mappings (targets.*, pushConstantWebGpuBinding.*) and sequences
// (entryPoints, bindings, vertexAttributes, pushConstantMembers) without the
// fragile regex/string scanning the old JSON loader relied on.
class ARP_API ShaderReflectionCache {
public:
    static ShaderReflectionCacheLoadResult loadFromYaml(const std::pmr::string& yaml);
    static ShaderReflectionCacheLoadResult loadFromFile(const std::pmr::string& path);
};

} // namespace arp::shader

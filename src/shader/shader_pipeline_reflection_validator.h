#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "shader/shader_compiler.h"

#include <string>
#include <vector>

namespace arp::shader {

struct ShaderReflectionModule;

struct ShaderPipelineLayoutDesc {
    uint32_t pushConstantSizeBytes = 0;
    std::pmr::vector<ShaderVertexAttribute> vertexAttributes;
};

struct ShaderPipelineValidationResult {
    bool success = false;
    std::pmr::string diagnostics;
};

class ARP_API ShaderPipelineReflectionValidator {
public:
    /// Validate the member-level ABI emitted for an AtomicFullBlockPush
    /// reflection artifact.  The member list is authoritative for the
    /// C++/shader byte layout; a total byte count alone is insufficient.
    static ShaderPipelineValidationResult validatePushConstantMembers(
        const ShaderReflection& reflection);

    /// Validate metadata that is mandatory only for a generated artifact.
    /// In-memory reflection used by layout unit tests is intentionally not
    /// subject to artifact freshness checks.
    static ShaderPipelineValidationResult validateGeneratedArtifact(
        const ShaderReflection& reflection);

    /// Validate the complete generated-artifact identity used by a shader
    /// package. Besides the reflection schema, this checks that the package
    /// points at the same source/target paths recorded by the generator and
    /// that the recorded source fingerprint matches the source bytes.
    static ShaderPipelineValidationResult validateGeneratedArtifact(
        const ShaderReflectionModule& artifact,
        const std::pmr::string& expectedSourcePath,
        const std::pmr::string& expectedSpirvPath,
        const std::pmr::string& expectedMslPath,
        const std::pmr::string& expectedWgslPath);

    static ShaderPipelineValidationResult validate(
        const ShaderPipelineLayoutDesc& pipeline, const ShaderReflection& reflection);
};

} // namespace arp::shader

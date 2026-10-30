#include "shader/webgl_shader_program_artifact.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    using namespace arp;
    const std::filesystem::path path = argc > 1 ? argv[1] : ARP_WEBGL_FRAGMENT_PROGRAM;
    const shader::WebGlShaderLoadBudget budget{{4u << 20, 32u << 20, 32u << 20, 32}, 256, 32, 1024};
    core::pmr::String diagnostic;
    const auto artifact = shader::WebGlShaderProgramArtifact::load(path,
        budget,
        core::makeShared<core::CpuMemoryQuota>(budget.original.maxCpuMetadataBytes),
        diagnostic);
    if (argc > 2) {
        if (artifact || diagnostic != argv[2]) {
            std::fprintf(stderr, "unexpected result: %s\n", diagnostic.c_str());
            return 1;
        }
        return 0;
    }
    if (!artifact)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(artifact && !shader::WebGlShaderProgramArtifact::validateOriginal(artifact));
    assert(artifact->vertex().inputs.size() == 1);
    assert(artifact->fragment().extensions.size() == 2);
    assert(artifact->fragment().uniformVectors == 11);
    assert(artifact->fragment().blocks.size() == 1 && artifact->fragment().blocks[0].size == 176);
    shader::WebGlShaderLimits limits{
        16, 16, 256, 224, 16, 16, 32, true, true, true, true, true, true};
    assert(std::strcmp(artifact->assess(limits), "SHADER-WEBGL1-REQUIRED-EXTENSION") == 0);
    limits.fragmentDepth = true;
    assert(!artifact->assess(limits));
    limits.derivatives = false;
    assert(artifact->assess(limits));
    assert(!artifact->nativeExecutionProven());
}

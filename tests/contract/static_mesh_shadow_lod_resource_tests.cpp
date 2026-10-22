#include "resource/static_mesh_shadow_lod_resource.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace arp;
    constexpr const char* sourceId = "efcb1ef3-207f-4118-a3cf-697881587581";
    constexpr const char* lodId = "efcb1ef3-207f-4118-a3cf-697881587582";
    resource::ResourceManager manager;
    resource::StaticMeshShadowLodResource candidate(core::pmr::String(lodId),
        core::pmr::String(sourceId),
        "missing-test-project",
        "missing-test-project/missing-test-manifest.yaml");
    const auto rejected = [&](resource::ResourceLoadContext context, const char* diagnostic) {
        const auto result = candidate.load(manager, context);
        assert(!result.success && result.state == resource::ResourceState::Failed);
        assert(result.diagnostics == diagnostic);
        assert(candidate.sources().empty() && candidate.processedByteCount() == 0);
        assert(candidate.sourceGeneration() == 0);
        assert(candidate.reorderedOriginalIndices().empty() && candidate.indexOrderVersion() == 0);
        assert(!manager.find<resource::StaticMeshShadowLodResource>(lodId).ready());
        bool inaccessible = false;
        try {
            candidate.candidate(1);
        } catch (const core::DiagnosticError& error) {
            inaccessible = std::strstr(error.what(), "CANDIDATE-LEVEL-INVALID") != nullptr;
        }
        assert(inaccessible);
    };
    resource::ResourceLoadContext context;
    rejected(context, "SHADOW-LOD-EXPLICIT-BUDGET-REQUIRED");
    context.requirePreloadedSources = true;
    context.maxDecodedBytes = 4096;
    rejected(context, "SHADOW-LOD-PREPARATION-ONLY");
    context.requirePreloadedSources = false;
    rejected(context, "SHADOW-LOD-ORIGINAL-NOT-READY");
    assert(manager
            .loadResource(
                core::makeUnique<resource::MeshAssetResource>(core::pmr::String(sourceId), 1.f))
            .success);
    rejected(context, "SHADOW-LOD-ORIGINAL-IDENTITY-REQUIRED");
}

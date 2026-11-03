#include "shader_source_task.h"

#include "resource/shader_artifact_resource.h"

namespace arp::survivors {
namespace {
constexpr const char* resourceId = "20000000-0000-4000-8000-000000009050";
constexpr examples::StartupResourceDiagnostics diagnostics{"SURVIVORS-STARTUP-RESOURCE-CANCELLED",
    "SURVIVORS-STARTUP-GRAPH-NOT-ACCEPTED",
    "SURVIVORS-STARTUP-RESOURCE-PREPARATION"};

resource::ResourceGraphExecutionRequest request(core::PoolHandle mount)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = resourceId;
    resource::ShaderArtifactResourceDesc desc;
    desc.publication = SceneShaderSources::publication;
    desc.origin = {core::io::FilePathScope::MountedResource, mount};
    desc.budget = shader::GeneratedShaderArtifactSetBudget{SceneShaderSources::maxFileBytes,
        SceneShaderSources::maxSourceBytes,
        SceneShaderSources::maxSourceBytes,
        8,
        4};
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<resource::ShaderArtifactResource>(
        resource::ResourceReference{resourceId,
            resource::ShaderArtifactResource::resourceTypeName(),
            "survivors.scene-shader",
            "Prototype Complete",
            {}},
        std::move(desc));
    result.graph.nodes.push_back(std::move(node));
    result.budget = {1,
        SceneShaderSources::maxFiles,
        SceneShaderSources::maxSourceBytes,
        SceneShaderSources::maxSourceBytes,
        2};
    return result;
}
} // namespace

ShaderSourceTask::ShaderSourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    core::PoolHandle mount,
    core::runtime::CancellationToken cancellation)
    : task_(files, queue, request(mount), diagnostics, std::move(cancellation))
{}

renderer::ShaderPackagePreparationDesc ShaderSourceTask::preparation() const
{
    renderer::ShaderPackagePreparationDesc result;
    if (phase() != examples::StartupResourcePhase::Ready)
        return result;
    const auto root = task_.resources().find<resource::ShaderArtifactResource>(resourceId);
    if (root.ready()) {
        result.publication = SceneShaderSources::publication;
        result.prepared = root.get()->generatedSet();
    }
    return result;
}
} // namespace arp::survivors

#include "editor/viewport/editor_viewport_shader_sources.h"

#include "core/io/filesystem_path.h"
#include "editor/viewport/editor_scene_viewport_environment.h"
#include "editor/viewport/editor_scene_viewport_sprite_contract.h"
#include "resource/resource_source_io.h"
#include "resource/shader_artifact_resource.h"

namespace arp::editor {
namespace {
constexpr std::array<const char*, 4> ids{"c2150000-0000-4000-8000-000000000001",
    "c2150000-0000-4000-8000-000000000002",
    "c2150000-0000-4000-8000-000000000003",
    "c2150000-0000-4000-8000-000000000004"};

class ViewportShaderBundle final : public resource::ResourceBase {
public:
    ViewportShaderBundle()
        : ResourceBase({ids.back(), "EditorViewportShaderBundle", {}, "Prototype Complete", {}})
    {
        for (std::size_t i = 0; i < EditorViewportShaderSources::publications.size(); ++i)
            addDependency({ids[i], "viewport-program"});
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        for (std::size_t i = 0; i < EditorViewportShaderSources::publications.size(); ++i) {
            const auto program = manager.find<resource::ShaderArtifactResource>(ids[i]);
            if (!program.ready() || !program.get()->generatedSet())
                return resource::resourceLoadFailure(*this, "EDITOR-VIEWPORT-SOURCES-INCOMPLETE");
        }
        return manager.admitResourceMetadata(*this, context.policy);
    }
};

} // namespace

resource::ResourceGraphExecutionRequest EditorViewportShaderSources::request(
    core::PoolHandle mount, core::runtime::CancellationToken cancellation)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = ids.back();
    result.cancellation = std::move(cancellation);
    for (std::size_t i = 0; i < EditorViewportShaderSources::publications.size(); ++i) {
        resource::ShaderArtifactResourceDesc desc;
        desc.publication = EditorViewportShaderSources::publications[i];
        desc.origin = {core::io::FilePathScope::MountedResource, mount};
        desc.budget =
            shader::GeneratedShaderArtifactSetBudget{EditorViewportShaderSources::maxFileBytes,
                EditorViewportShaderSources::maxSourceBytes,
                8u * 1024u * 1024u,
                256,
                8};
        resource::ResourceGraphNode node;
        node.resource = core::makeUnique<resource::ShaderArtifactResource>(
            resource::ResourceReference{ids[i],
                resource::ShaderArtifactResource::resourceTypeName(),
                {},
                "Prototype Complete",
                {}},
            std::move(desc));
        result.graph.nodes.push_back(std::move(node));
    }
    resource::ResourceGraphNode root;
    root.resource = core::makeUnique<ViewportShaderBundle>();
    result.graph.nodes.push_back(std::move(root));
    result.budget = {ids.size(),
        EditorViewportShaderSources::maxFiles,
        EditorViewportShaderSources::maxSourceBytes,
        EditorViewportShaderSources::maxPayloadBytes,
        2};
    return result;
}

bool EditorViewportShaderSources::valid() const
{
    for (std::size_t i = 0; i < programs.size(); ++i)
        if (programs[i].publication != publications[i]
            || shader::GeneratedShaderArtifactSet::validateOriginal(programs[i].prepared))
            return false;
    // Source success alone does not prove the Editor's three distinct ABIs.
    // Reject them together before the host creates any window/GPU resources.
    return loadEditorSceneViewportShaderPackage(
               artifacts(0), rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1)
               .valid()
        && validateEditorSceneViewportSpriteReflection(programs[1].prepared->module().reflection)
        && validateEditorSceneViewportEnvironmentReflection(
            programs[2].prepared->module().reflection);
}

EditorViewportShaderSources EditorViewportShaderSources::capture(
    const resource::ResourceManager& manager)
{
    EditorViewportShaderSources result;
    const auto bundle = manager.find<ViewportShaderBundle>(ids.back());
    if (!bundle.ready())
        return result;
    for (std::size_t i = 0; i < result.programs.size(); ++i) {
        const auto handle = manager.find<resource::ShaderArtifactResource>(ids[i]);
        if (handle.ready()) {
            result.programs[i].publication = publications[i];
            result.programs[i].prepared = handle.get()->generatedSet();
            for (std::size_t role = 0; role < result.paths[i].size(); ++role)
                result.paths[i][role] =
                    core::FilePath::utf8(result.programs[i].publication.parent_path()
                        / result.programs[i].prepared->filename(
                            static_cast<shader::ShaderArtifactRole>(role)));
        }
    }
    return result;
}

EditorSceneViewportShaderArtifacts EditorViewportShaderSources::artifacts(std::size_t index) const
{
    const auto& values = paths.at(index);
    return {values[0], values[1], values[2], values[3], values[4], programs.at(index)};
}
} // namespace arp::editor

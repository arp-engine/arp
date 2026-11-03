#include "editor/ui/editor_ui_sources.h"

#include "editor/ui/editor_ui_content_preparation.h"
#include "renderer/gui/gui_draw_plan.h"
#include "renderer/renderer_common.h"
#include "resource/resource_source_io.h"
#include "resource/shader_artifact_resource.h"

namespace arp::editor {
namespace {
using Limits = EditorUiSourceLimits;
constexpr const char* guiId = "c2260000-0000-4000-8000-000000000001";
constexpr const char* bundleId = "c2260000-0000-4000-8000-000000000002";

constexpr EditorUiContentProfile profile{Limits::profile, Limits::guiPublication, false};

class UiSourcesResource final : public resource::ResourceBase {
public:
    explicit UiSourcesResource(EditorUiSourceMounts mounts)
        : ResourceBase({bundleId, "EditorUiSources", {}, "Prototype Complete", {}}), mounts_(mounts)
    {
        addDependency({guiId, "gui-shader"});
    }

    bool planFileReadIntents(resource::ResourceManager&,
        const resource::ResourceLoadContext& context,
        core::pmr::Vector<resource::ResourceFileReadIntent>& reads,
        core::pmr::String& diagnostic) const override
    {
        auto add = [&](const core::pmr::String& path, core::PoolHandle mount, std::size_t maximum) {
            resource::ResourceFileReadIntent intent;
            intent.path = path;
            intent.pathScope = core::io::FilePathScope::MountedResource;
            intent.mount = mount;
            intent.maxBytes = maximum;
            reads.push_back(std::move(intent));
        };
        add(Limits::profile, mounts_.ui, Limits::maxProfileBytes);
        if (!context.sources || !context.sources->find(Limits::profile))
            return true;
        EditorUiContentBindings binding;
        if (!EditorUiContentPreparation::parse(
                context, {mounts_.ui, mounts_.fonts}, profile, binding, diagnostic))
            return false;
        add(binding.document, mounts_.ui, Limits::maxDocumentBytes);
        add(binding.atlas, mounts_.ui, Limits::maxAtlasBytes);
        for (std::size_t i = 0; i < binding.fontPaths.size(); ++i)
            add(binding.fontPaths[i], mounts_.fonts, binding.fontBudgets[i]);
        return true;
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        core::pmr::String diagnostic;
        EditorUiContentBindings binding;
        if (!context.requirePreloadedSources
            || !EditorUiContentPreparation::parse(
                context, {mounts_.ui, mounts_.fonts}, profile, binding, diagnostic))
            return resource::resourceLoadFailure(*this,
                diagnostic.empty() ? "EDITOR-UI-SOURCES-PRELOADED-REQUIRED" : diagnostic.c_str());
        const auto shader = manager.find<resource::ShaderArtifactResource>(guiId);
        if (!shader.ready() || !shader.get()->generatedSet())
            return resource::resourceLoadFailure(*this, "EDITOR-UI-GUI-SHADER-MISSING");
        auto candidate = core::makeShared<EditorUiSources>();
        if (!EditorUiContentPreparation::prepare(
                context, {mounts_.ui, mounts_.fonts}, binding, *candidate, diagnostic))
            return resource::resourceLoadFailure(*this, diagnostic.c_str());
        candidate->guiShader.publication = Limits::guiPublication;
        candidate->guiShader.prepared = shader.get()->generatedSet();
        if (!candidate->valid(diagnostic))
            return resource::resourceLoadFailure(*this, diagnostic.c_str());
        data_ = std::move(candidate);
        const auto admitted = manager.admitResourceMetadata(*this, context.policy);
        if (!admitted.success)
            data_.reset();
        return admitted;
    }

    std::size_t processedByteCount() const noexcept override
    {
        return data_ ? data_->processedByteCount() : 0;
    }

    core::SharedPtr<const EditorUiSources> data() const { return data_; }

private:
    EditorUiSourceMounts mounts_;
    core::SharedPtr<const EditorUiSources> data_;
};
} // namespace

bool EditorUiSources::valid(core::pmr::String& diagnostic) const
{
    if (!EditorUiContent::valid(diagnostic))
        return false;
    rhi::ShaderPackageDesc package;
    const auto prepared = renderer::detail::prepareGraphicsShaderPackage(
        package, "EditorGui", "gui_overlay.slang", sizeof(renderer::GuiPushConstants), guiShader);
    if (!prepared.success) {
        diagnostic = prepared.diagnostics;
        return false;
    }
    return true;
}

resource::ResourceGraphExecutionRequest EditorUiSources::request(
    EditorUiSourceMounts mounts, core::runtime::CancellationToken cancellation)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = bundleId;
    result.cancellation = std::move(cancellation);
    resource::ShaderArtifactResourceDesc shaderDesc;
    shaderDesc.publication = Limits::guiPublication;
    shaderDesc.origin = {core::io::FilePathScope::MountedResource, mounts.shaders};
    shaderDesc.budget = shader::GeneratedShaderArtifactSetBudget{
        Limits::maxShaderFileBytes, Limits::maxSourceBytes, 8u * 1024u * 1024u, 64, 8};
    resource::ResourceGraphNode program;
    program.resource = core::makeUnique<resource::ShaderArtifactResource>(
        resource::ResourceReference{guiId,
            resource::ShaderArtifactResource::resourceTypeName(),
            {},
            "Prototype Complete",
            {}},
        std::move(shaderDesc));
    result.graph.nodes.push_back(std::move(program));
    resource::ResourceGraphNode root;
    root.resource = core::makeUnique<UiSourcesResource>(mounts);
    result.graph.nodes.push_back(std::move(root));
    result.budget = {2, 32, Limits::maxSourceBytes, Limits::maxPayloadBytes, 2};
    return result;
}

core::SharedPtr<const EditorUiSources> EditorUiSources::capture(
    const resource::ResourceManager& manager)
{
    const auto root = manager.find<UiSourcesResource>(bundleId);
    return root.ready() ? root.get()->data() : nullptr;
}
} // namespace arp::editor

#include "editor/web/editor_web_startup_sources.h"

#include "editor/ui/editor_ui_content_preparation.h"
#include "resource/resource_source_io.h"
#include "resource/shader_artifact_resource.h"

namespace arp::editor {
namespace {
using Limits = EditorWebStartupLimits;
constexpr const char* rootId = "c2280000-0000-4000-8000-000000000001";
constexpr EditorUiContentProfile profile{Limits::profile, Limits::programs[0], true};

shader::GraphicsShaderModuleLoadBudget moduleBudget()
{
    return {Limits::maxShaderFileBytes,
        Limits::maxSourceBytes,
        Limits::maxShaderMetadataBytes,
        64,
        64,
        512,
        16};
}

class WebStartupResource final : public resource::ResourceBase {
public:
    explicit WebStartupResource(EditorWebStartupMounts mounts)
        : ResourceBase({rootId, "EditorWebStartupSources", {}, "Prototype Complete", {}}),
          mounts_(mounts)
    {}

    bool planFileReadIntents(resource::ResourceManager& manager,
        const resource::ResourceLoadContext& context,
        core::pmr::Vector<resource::ResourceFileReadIntent>& pending,
        core::pmr::String& diagnostic) const override
    {
        // Process each original publication to completion before the next one.
        // Shared module/entry dependencies then already exist in the source set;
        // no duplicate-intent merger or second Shader metadata parser is needed.
        const resource::ResourceFileOrigin uiOrigin{
            core::io::FilePathScope::MountedResource, mounts_.ui};
        if (!resource::appendResourceReadIntent(
                Limits::profile, Limits::maxProfileBytes, pending, diagnostic, uiOrigin))
            return false;
        if (!context.sources || !context.sources->find(Limits::profile))
            return true;
        pending.clear();
        EditorUiContentBindings binding;
        if (!EditorUiContentPreparation::parse(
                context, {mounts_.ui, mounts_.fonts}, profile, binding, diagnostic))
            return false;
        auto addMissing =
            [&](const core::pmr::String& path, core::PoolHandle mount, std::size_t maximum) {
                if (context.sources->find(path))
                    return true;
                return resource::appendResourceReadIntent(path,
                    maximum,
                    pending,
                    diagnostic,
                    {core::io::FilePathScope::MountedResource, mount});
            };
        if (!addMissing(binding.document, mounts_.ui, Limits::maxDocumentBytes)
            || !addMissing(binding.atlas, mounts_.ui, Limits::maxAtlasBytes))
            return false;
        for (std::size_t i = 0; i < binding.fontPaths.size(); ++i) {
            bool alias = false;
            for (std::size_t j = 0; j < i; ++j)
                alias |= binding.fontPaths[i] == binding.fontPaths[j];
            if (!alias && !addMissing(binding.fontPaths[i], mounts_.fonts, binding.fontBudgets[i]))
                return false;
        }
        if (!pending.empty())
            return true;
        const resource::ResourceFileOrigin shaderOrigin{
            core::io::FilePathScope::MountedResource, mounts_.shaders};
        for (const auto* publication : Limits::programs) {
            resource::ShaderArtifactResource planner(
                reference(), {publication, shaderOrigin, Limits::programBudget(), {}});
            if (!planner.planFileReadIntents(manager, context, pending, diagnostic))
                return false;
            if (!pending.empty())
                return true;
        }
        for (const auto* publication : Limits::modules) {
            resource::ShaderArtifactResource planner(
                reference(), {publication, shaderOrigin, moduleBudget(), {}});
            if (!planner.planFileReadIntents(manager, context, pending, diagnostic))
                return false;
            if (!pending.empty())
                return true;
        }
        return true;
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        core::pmr::String diagnostic;
        auto candidate = core::makeShared<EditorWebStartupSources>(resource::ResourceFileOrigin{
            core::io::FilePathScope::MountedResource, mounts_.shaders});
        if (!candidate->prepare(context, mounts_, diagnostic))
            return resource::resourceLoadFailure(*this, diagnostic.c_str());
        data_ = std::move(candidate);
        const auto result = manager.admitResourceMetadata(*this, context.policy);
        if (!result.success)
            data_.reset();
        return result;
    }

    std::size_t processedByteCount() const noexcept override
    {
        return data_ ? data_->processedByteCount() : 0;
    }

    core::SharedPtr<const EditorWebStartupSources> data() const { return data_; }

private:
    EditorWebStartupMounts mounts_;
    core::SharedPtr<const EditorWebStartupSources> data_;
};
} // namespace

shader::WebGlShaderLoadBudget EditorWebStartupLimits::programBudget()
{
    return {
        {maxShaderFileBytes, maxShaderMetadataBytes, maxShaderMetadataBytes, 32}, 256, 32, 1024};
}

EditorWebStartupSources::EditorWebStartupSources(resource::ResourceFileOrigin origin)
    : projection_(context_, origin, nullptr)
{
    context_.sources = &files_;
    context_.requirePreloadedSources = true;
    context_.maxDecodedBytes = Limits::maxPayloadBytes;
}

bool EditorWebStartupSources::prepare(const resource::ResourceLoadContext& context,
    EditorWebStartupMounts mounts,
    core::pmr::String& diagnostic)
{
    if (!context.requirePreloadedSources || !context.sources
        || context.sources->size() > Limits::maxFiles
        || context.sources->totalBytes() > Limits::maxSourceBytes) {
        diagnostic = "EDITOR-WEB-STARTUP-SOURCES-INCOMPLETE";
        return false;
    }
    EditorUiContentBindings binding;
    if (!EditorUiContentPreparation::parse(
            context, {mounts.ui, mounts.fonts}, profile, binding, diagnostic)
        || !EditorUiContentPreparation::prepare(
            context, {mounts.ui, mounts.fonts}, binding, ui_, diagnostic))
        return false;
    // SourceSet indexes logical paths. Only Shader inputs belong to this pure
    // projection; UI/font bytes are retained by their own semantic owners.
    for (std::size_t i = 0; i < context.sources->size(); ++i) {
        const auto& file = *context.sources->sourceAt(i);
        if (file.role == "shader-artifact" && !files_.add(file)) {
            diagnostic = "EDITOR-WEB-STARTUP-SHADER-AMBIGUOUS";
            return false;
        }
    }
    for (std::size_t i = 0; i < Limits::programs.size(); ++i) {
        auto budget = Limits::programBudget();
        budget.original.source = &projection_;
        const auto program = shader::WebGlShaderProgramArtifact::load(Limits::programs[i],
            budget,
            core::makeShared<core::CpuMemoryQuota>(budget.original.maxCpuMetadataBytes),
            diagnostic);
        if (!program)
            return false;
        if (i < 2)
            continue;
        auto combinedBudget = moduleBudget();
        combinedBudget.source = &projection_;
        const auto module = shader::GraphicsShaderModuleArtifact::load(Limits::modules[i - 2],
            combinedBudget,
            core::makeShared<core::CpuMemoryQuota>(combinedBudget.maxCpuMetadataBytes),
            diagnostic);
        if (!module)
            return false;
        // Full-module ABI consumers and the actual ES100 selected entries must
        // come from precisely the same original compiler identities.
        if (module->vertex()->identity() != program->vertex().original->identity()
            || module->fragment()->identity() != program->fragment().original->identity()) {
            diagnostic = "EDITOR-WEB-STARTUP-MODULE-PROGRAM-IDENTITY";
            return false;
        }
        reflections_[i - 2] = module->module().reflection;
    }
    return true;
}

std::size_t EditorWebStartupSources::processedByteCount() const noexcept
{
    return ui_.processedByteCount() + files_.totalBytes();
}

resource::ResourceGraphExecutionRequest EditorWebStartupSources::request(
    EditorWebStartupMounts mounts, core::runtime::CancellationToken cancellation)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = rootId;
    result.cancellation = std::move(cancellation);
    resource::ResourceGraphNode root;
    root.resource = core::makeUnique<WebStartupResource>(mounts);
    result.graph.nodes.push_back(std::move(root));
    result.budget = {1, Limits::maxFiles, Limits::maxSourceBytes, Limits::maxPayloadBytes, 1};
    return result;
}

core::SharedPtr<const EditorWebStartupSources> EditorWebStartupSources::capture(
    const resource::ResourceManager& manager)
{
    const auto root = manager.find<WebStartupResource>(rootId);
    return root.ready() ? root.get()->data() : nullptr;
}
} // namespace arp::editor

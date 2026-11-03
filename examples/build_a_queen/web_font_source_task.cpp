#include "web_font_source_task.h"

#include "resource/resource_source_io.h"

namespace arp::build_a_queen {

core::SharedPtr<const WebFontResources> WebFontResources::prepare(
    const resource::ResourceLoadContext& context,
    resource::ResourceFileOrigin origin,
    core::pmr::String& diagnostic)
{
    // This entry is deliberately source-only, even when invoked by a native test.
    if (!context.requirePreloadedSources || !context.sources) {
        diagnostic = "QUEEN-FONT-SOURCES-NOT-PREPARED";
        return {};
    }
    auto candidate = core::makeShared<WebFontResources>();
    auto result = candidate->resources_.load<scene2d::SpriteAtlasResource>(context,
        core::pmr::String(atlasId),
        core::pmr::String(inputs[0]),
        core::pmr::String(inputs[1]),
        maxFileBytes,
        origin);
    if (result.success)
        result = candidate->resources_.load<ui::UiBitmapFontResource>(context,
            core::pmr::String(fontId),
            core::pmr::String(inputs[2]),
            core::pmr::String(atlasId),
            128 * 1024,
            95);
    if (!result.success) {
        diagnostic = std::move(result.diagnostics);
        return {};
    }
    return candidate;
}

const ui::UiBitmapFontResource* WebFontResources::font() const noexcept
{
    return resources_.find<ui::UiBitmapFontResource>(fontId).get();
}

namespace {
constexpr const char* rootId = "c677046b-b3b6-4467-9c5b-645ee40d4cf9";

// The three paths are the product's explicit bitmap-font binding. Existing
// resource loaders remain the only YAML/PNG/glyph validators.
class FontResource final : public resource::ResourceBase {
public:
    static constexpr const char* resourceTypeName() { return "QueenWebFontResources"; }

    explicit FontResource(resource::ResourceFileOrigin origin)
        : ResourceBase({rootId, resourceTypeName(), "queen.web.font", "Prototype Complete", {}}),
          origin_(origin)
    {}

    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>& intents,
        core::pmr::String& diagnostic) const override
    {
        for (const auto* path : WebFontResources::inputs)
            if (!resource::appendResourceReadIntent(
                    path, WebFontResources::maxFileBytes, intents, diagnostic, origin_))
                return false;
        return true;
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        core::pmr::String diagnostic;
        auto candidate = WebFontResources::prepare(context, origin_, diagnostic);
        if (!candidate)
            return resource::resourceLoadFailure(*this, diagnostic.c_str());
        prepared_ = std::move(candidate);
        return manager.admitResourceMetadata(*this, context.policy);
    }

    const core::SharedPtr<const WebFontResources>& prepared() const noexcept { return prepared_; }

private:
    resource::ResourceFileOrigin origin_;
    core::SharedPtr<const WebFontResources> prepared_;
};

resource::ResourceGraphExecutionRequest request(core::PoolHandle mount)
{
    resource::ResourceGraphExecutionRequest result;
    result.graph.rootResourceId = rootId;
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<FontResource>(
        resource::ResourceFileOrigin{core::io::FilePathScope::MountedResource, mount});
    result.graph.nodes.push_back(std::move(node));
    result.budget = {1,
        WebFontResources::inputs.size(),
        WebFontResources::maxSourceBytes,
        WebFontResources::maxDecodedBytes,
        2};
    return result;
}
} // namespace

WebFontSourceTask::WebFontSourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    core::PoolHandle mount,
    core::runtime::CancellationToken cancellation)
    : task_(files, queue, request(mount), queenStartupDiagnostics, std::move(cancellation))
{}

void WebFontSourceTask::advance() noexcept
{
    task_.advance();
}

void WebFontSourceTask::cancel() noexcept
{
    task_.cancel();
}

WebFontSourcePhase WebFontSourceTask::phase() const noexcept
{
    return task_.phase();
}

std::string_view WebFontSourceTask::diagnostic() const noexcept
{
    return phase() == WebFontSourcePhase::Cancelled ? "QUEEN-FONT-SOURCES-CANCELLED"
                                                    : task_.diagnostic();
}

std::uint64_t WebFontSourceTask::readCount() const noexcept
{
    return task_.readCount();
}

core::SharedPtr<const WebFontResources> WebFontSourceTask::resources() const noexcept
{
    const auto* root = phase() == WebFontSourcePhase::Ready
        ? task_.resources().find<FontResource>(rootId).get()
        : nullptr;
    return root ? root->prepared() : nullptr;
}
} // namespace arp::build_a_queen

#include "native_hud_source_task.h"

#include "hud_content.h"
#include "resource/resource_source_io.h"
#include "ui/ui_atlas_metadata_resource.h"
#include "ui/ui_font_set_resource.h"
#include "ui/ui_font_source.h"

#include <array>

namespace arp::build_a_queen {
namespace {
constexpr std::array<const char*, 4> ids{"26ae1346-f5b0-42c3-b285-d6e8a76ce020",
    "26ae1346-f5b0-42c3-b285-d6e8a76ce021",
    "26ae1346-f5b0-42c3-b285-d6e8a76ce022",
    "26ae1346-f5b0-42c3-b285-d6e8a76ce023"};
constexpr const char* metadataAlias = "queen.native.hud.metadata";
constexpr const char* atlasResource = "queen.ui.framework.atlas";

class HudResource final : public resource::ResourceBase {
public:
    static constexpr const char* resourceTypeName() { return "QueenNativeHudSources"; }

    HudResource()
        : ResourceBase({ids[3], resourceTypeName(), "queen.native.hud", "Prototype Complete", {}})
    {
        addDependency({ids[1], "font-set"});
        addDependency({ids[2], "base-atlas"});
    }

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        const auto fonts = manager.find<ui::UiFontSetResource>(ids[1]);
        const auto atlas = manager.find<ui::UiAtlasImageResource>(ids[2]);
        if (!fonts.ready() || !atlas.ready() || !fonts.get()->data
            || fonts.get()->data->fonts.size() != 1 || !atlas.get()->data)
            return resource::resourceLoadFailure(*this, "QUEEN-HUD-SOURCES-INCOMPLETE");
        const auto& desc = fonts.get()->data->fonts.front();
        core::pmr::String diagnostic;
        if (desc.tokenId != kQueenFontToken || !ui::validatePreparedUiFontFace(desc, diagnostic))
            return resource::resourceLoadFailure(
                *this, diagnostic.empty() ? "QUEEN-HUD-FONT-BINDING" : diagnostic.c_str());
        const auto loaded = ui::UiFontFaceLoader().load(desc);
        if (!loaded.ok())
            return resource::resourceLoadFailure(*this, loaded.diagnostic().message.c_str());
        auto candidate = core::makeShared<NativeHudSources>();
        candidate->face = loaded.face();
        candidate->atlas = atlas.get()->data;
        if (!candidate->valid())
            return resource::resourceLoadFailure(*this, "QUEEN-HUD-RESOURCE-BINDING");
        prepared_ = std::move(candidate);
        return manager.admitResourceMetadata(*this, context.policy);
    }

    const auto& prepared() const noexcept { return prepared_; }

private:
    core::SharedPtr<const NativeHudSources> prepared_;
};

resource::ResourceGraphExecutionRequest request(core::PoolHandle mount)
{
    resource::ResourceGraphExecutionRequest result;
    const resource::ResourceFileOrigin origin{core::io::FilePathScope::MountedResource, mount};
    ui::UiAtlasMetadataResourceLoadDesc metadata;
    metadata.resourceId = ids[0];
    metadata.alias = metadataAlias;
    metadata.status = "Prototype Complete";
    metadata.atlasMetadataPath = NativeHudSources::metadataFile;
    metadata.expectedAtlasResourceId = atlasResource;
    metadata.maxMetadataBytes = 128 * 1024;
    metadata.origin = origin;
    resource::ResourceGraphNode node;
    node.resource = core::makeUnique<ui::UiAtlasMetadataResource>(std::move(metadata));
    result.graph.nodes.push_back(std::move(node));
    ui::UiFontSetResourceLoadDesc fonts;
    fonts.resourceId = ids[1];
    fonts.status = "Prototype Complete";
    fonts.atlasMetadataAlias = metadataAlias;
    fonts.requiredGlyphPolicy = "hybrid";
    fonts.requiredPagePolicy = "singleTexture";
    fonts.maxFaces = 1;
    fonts.maxFontBytes = fonts.maxTotalSourceBytes = NativeHudSources::maxFileBytes;
    fonts.origin = origin;
    node = {};
    node.resource = core::makeUnique<ui::UiFontSetResource>(std::move(fonts));
    node.dependencies.push_back({ids[0], "font-metadata"});
    result.graph.nodes.push_back(std::move(node));
    ui::UiAtlasImageResourceLoadDesc atlas;
    atlas.resourceId = ids[2];
    atlas.status = "Prototype Complete";
    atlas.atlasMetadataAlias = metadataAlias;
    atlas.maxEncodedBytes = 128 * 1024;
    atlas.maxDecodedBytes = 4 * 1024;
    atlas.origin = origin;
    node = {};
    node.resource = core::makeUnique<ui::UiAtlasImageResource>(std::move(atlas));
    node.dependencies.push_back({ids[0], "atlas-metadata"});
    result.graph.nodes.push_back(std::move(node));
    node = {};
    node.resource = core::makeUnique<HudResource>();
    result.graph.nodes.push_back(std::move(node));
    result.graph.rootResourceId = ids[3];
    result.budget = {
        ids.size(), 3, NativeHudSources::maxSourceBytes, NativeHudSources::maxDecodedBytes, 2};
    return result;
}
} // namespace

bool NativeHudSources::valid() const noexcept
{
    return face.validatedByFreeType && face.desc.tokenId == kQueenFontToken && face.desc.source
        && atlas && atlas->atlasResourceId == atlasResource && atlas->width && atlas->height
        && std::uint64_t(atlas->width) * atlas->height * 4 == atlas->rgba8.size();
}

NativeHudSourceTask::NativeHudSourceTask(core::io::FileService& files,
    core::runtime::CompletionQueue& queue,
    core::PoolHandle mount,
    core::runtime::CancellationToken cancellation)
    : task_(files, queue, request(mount), queenStartupDiagnostics, std::move(cancellation))
{}

void NativeHudSourceTask::advance() noexcept
{
    task_.advance();
}

void NativeHudSourceTask::cancel() noexcept
{
    task_.cancel();
}

StartupResourcePhase NativeHudSourceTask::phase() const noexcept
{
    return task_.phase();
}

std::string_view NativeHudSourceTask::diagnostic() const noexcept
{
    return task_.diagnostic();
}

std::uint64_t NativeHudSourceTask::readCount() const noexcept
{
    return task_.readCount();
}

core::SharedPtr<const NativeHudSources> NativeHudSourceTask::sources() const noexcept
{
    const auto* root = phase() == StartupResourcePhase::Ready
        ? task_.resources().find<HudResource>(ids[3]).get()
        : nullptr;
    return root ? root->prepared() : nullptr;
}
} // namespace arp::build_a_queen

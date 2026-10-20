#include "ui/ui_bitmap_font_resource.h"

#include "core/sha256.h"
#include "core/utf8.h"
#include "core/yaml_contract.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::ui {
UiBitmapFontResource::UiBitmapFontResource(core::pmr::String id,
    core::pmr::String path,
    core::pmr::String atlasId,
    std::size_t maxBytes,
    std::uint32_t maxGlyphs)
    : ResourceBase(resource::authoredResourceReference(std::move(id), "UiBitmapFontResource")),
      metadataPath_(std::move(path)), atlasId_(std::move(atlasId)), maxMetadataBytes_(maxBytes),
      maxGlyphs_(maxGlyphs)
{
    addDependency({atlasId_, "bitmap-font-atlas"});
}

bool UiBitmapFontResource::collectFileReadIntents(resource::ResourceManager&,
    core::pmr::Vector<resource::ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostics) const
{
    return resource::appendResourceReadIntent(
        metadataPath_, maxMetadataBytes_, intents, diagnostics);
}

resource::ResourceLoadResult UiBitmapFontResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& context)
{
    const auto fail = [&](const char* id) {
        return resource::resourceLoadFailure(*this, id);
    };
    glyphs_.clear();
    atlas_ = {};
    if (!maxMetadataBytes_ || !maxGlyphs_ || !resource::isResourceUuid(reference().id)
        || !resource::isResourceUuid(atlasId_))
        return fail("UI-BITMAP-FONT-CONFIG");
    auto atlas = manager.find<scene2d::SpriteAtlasResource>(atlasId_);
    if (!atlas.ready())
        return fail("UI-BITMAP-FONT-ATLAS-NOT-READY");
    const auto source = resource::readResourceSource(metadataPath_, context, maxMetadataBytes_);
    if (!source.success)
        return fail(source.diagnostics.c_str());
    try {
        using Y = core::YamlContract;
        const auto document = Y::Node::deserialize(core::pmr::String(
            reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()));
        if (Y::text(document, "type") != "arp.ui-bitmap-font"
            || Y::uint32(document, "contractVersion") != 1
            || Y::text(document, "resourceId") != reference().id
            || Y::text(document, "atlasResourceId") != atlasId_
            || Y::text(document, "cookProfile") != "UiBitmapFontCook/1"
            || Y::text(document, "format") != "Rgba8Unorm"
            || Y::text(document, "alpha") != "straight" || Y::text(document, "filter") != "nearest"
            || Y::boolean(document, "srgb")
            || Y::text(document, "glyphPolicy") != "explicit-static-codepoints")
            return fail("UI-BITMAP-FONT-COOK-PROFILE");
        const auto token = Y::text(document, "fontToken");
        const auto page = Y::uint32(document, "atlasPage"), size = Y::uint32(document, "sizePx");
        if (token.empty() || core::Utf8Decoder::visit(token, [](std::uint32_t) noexcept {})
            || !size)
            return fail("UI-BITMAP-FONT-METRICS");
        const auto& image = atlas.get()->image();
        const auto hash = Y::text(document, "rgba8Sha256");
        if (image.srgb
            || !core::Sha256::matchesHexDigest(
                {reinterpret_cast<const char*>(image.rgba8.data()), image.rgba8.size()}, hash))
            return fail("UI-BITMAP-FONT-PIXEL-IDENTITY");
        const auto& nodes = Y::sequence(document, "glyphs");
        if (nodes.empty() || nodes.size() > maxGlyphs_)
            return fail("UI-BITMAP-FONT-GLYPH-BUDGET");
        core::pmr::Vector<UiBitmapGlyph> glyphs{&core::defaultCpuMemoryResource()};
        glyphs.reserve(nodes.size());
        for (const auto& node : nodes) {
            UiBitmapGlyph g;
            g.codepoint = Y::uint32(node, "codepoint");
            core::pmr::String scalar{&core::defaultCpuMemoryResource()},
                diagnostic{&core::defaultCpuMemoryResource()};
            if (!core::appendUtf8Codepoint(scalar, g.codepoint, diagnostic))
                return fail("UI-BITMAP-FONT-CODEPOINT");
            g.sliceName = Y::text(node, "slice");
            g.advanceX = static_cast<float>(Y::number(node, "advanceX"));
            g.bearingX = static_cast<float>(Y::number(node, "bearingX"));
            g.bearingY = static_cast<float>(Y::number(node, "bearingY"));
            if (!std::isfinite(g.advanceX) || g.advanceX <= 0 || !std::isfinite(g.bearingX)
                || !std::isfinite(g.bearingY))
                return fail("UI-BITMAP-FONT-METRICS");
            if (!atlas.get()->slice(g.sliceName))
                return fail("UI-BITMAP-FONT-SLICE-MISSING");
            if (std::any_of(glyphs.begin(), glyphs.end(), [&](const auto& old) {
                    return old.codepoint == g.codepoint;
                }))
                return fail("UI-BITMAP-FONT-CODEPOINT-DUPLICATE");
            glyphs.push_back(std::move(g));
        }
        auto admitted = manager.admitResourceMetadata(*this, context.policy);
        if (!admitted.success)
            return admitted;
        fontToken_ = token;
        page_ = page;
        sizePx_ = size;
        atlas_ = atlas;
        glyphs_ = std::move(glyphs);
        return admitted;
    } catch (const std::bad_alloc&) {
        return fail("UI-BITMAP-FONT-CPU-ALLOCATION");
    } catch (...) {
        return fail("UI-BITMAP-FONT-YAML-CONTRACT");
    }
}

const UiBitmapGlyph* UiBitmapFontResource::glyph(std::uint32_t codepoint) const
{
    if (!valid())
        return nullptr;
    const auto found = std::find_if(glyphs_.begin(), glyphs_.end(), [&](const auto& glyph) {
        return glyph.codepoint == codepoint;
    });
    return found == glyphs_.end() ? nullptr : &*found;
}
} // namespace arp::ui

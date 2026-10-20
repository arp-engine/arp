#pragma once

#include "2d/sprite_resources.h"

namespace arp::ui {

struct UiBitmapGlyph {
    std::uint32_t codepoint = 0;
    core::pmr::String sliceName{&core::defaultCpuMemoryResource()};
    float advanceX = 0, bearingX = 0, bearingY = 0;
};

// Explicit static glyph resource. String shaping, glyph selection and geometry
// remain caller intent; this resource never guesses metrics or font fallbacks.
class UiBitmapFontResource final : public resource::ResourceBase {
public:
    UiBitmapFontResource(core::pmr::String id,
        core::pmr::String metadataPath,
        core::pmr::String atlasId,
        std::size_t maxMetadataBytes,
        std::uint32_t maxGlyphs);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;
    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String&) const override;

    bool valid() const { return atlas_.ready() && !glyphs_.empty(); }

    const UiBitmapGlyph* glyph(std::uint32_t codepoint) const;

    const auto& atlas() const { return atlas_; }

    const auto& fontToken() const { return fontToken_; }

    std::uint32_t page() const { return page_; }

    std::uint32_t sizePx() const { return sizePx_; }

private:
    core::pmr::String metadataPath_, atlasId_, fontToken_{&core::defaultCpuMemoryResource()};
    std::size_t maxMetadataBytes_;
    std::uint32_t maxGlyphs_, page_ = 0, sizePx_ = 0;
    resource::ResourceHandle<scene2d::SpriteAtlasResource> atlas_;
    core::pmr::Vector<UiBitmapGlyph> glyphs_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::ui

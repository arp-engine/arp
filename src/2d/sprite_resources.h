#pragma once
#include "assets/image_decoder.h"
#include "core/math.h"
#include "resource/resource_manager.h"

namespace arp::scene2d {
struct SpriteSlice {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    core::Vec2 pivot{};
    float pixelsPerUnit = 0;
};

class SpriteAtlasResource final : public resource::ResourceBase {
public:
    SpriteAtlasResource(core::pmr::String id,
        core::pmr::String metadataPath,
        core::pmr::String pngPath,
        std::size_t maxBytes);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;
    bool collectFileReadIntents(resource::ResourceManager&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String&) const override;

    const assets::DecodedImage& image() const { return image_; }

    const SpriteSlice* slice(std::string_view name) const;

    std::size_t processedByteCount() const noexcept override { return image_.rgba8.size(); }

private:
    core::pmr::String metadataPath_, pngPath_;
    std::size_t maxBytes_;
    assets::DecodedImage image_;
    core::pmr::Vector<SpriteSlice> slices_{&core::defaultCpuMemoryResource()};
};

class SpriteResource final : public resource::ResourceBase {
public:
    SpriteResource(core::pmr::String id, core::pmr::String atlasId, core::pmr::String sliceName);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;

    const core::pmr::String& atlasId() const { return atlasId_; }

    const core::pmr::String& sliceName() const { return sliceName_; }

    const auto& atlas() const { return atlas_; }

    const SpriteSlice* slice() const { return atlas_.ready() ? slice_ : nullptr; }

private:
    core::pmr::String atlasId_, sliceName_;
    resource::ResourceHandle<SpriteAtlasResource> atlas_;
    const SpriteSlice* slice_ = nullptr;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SpriteFontGlyphDesc {
    APROP(Serialize)
    std::uint32_t codepoint = 0;
    APROP(Serialize)
    core::pmr::String spriteResourceId{&core::defaultCpuMemoryResource()};
    APROP(Serialize)
    float advance = 0.0f;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct SpriteFontResourceDesc {
    APROP(Serialize)
    std::uint32_t maxTextBytes = 0;
    APROP(Serialize)
    core::pmr::Vector<SpriteFontGlyphDesc> glyphs{&core::defaultCpuMemoryResource()};
};

struct SpriteFontGlyph {
    std::uint32_t codepoint = 0;
    resource::ResourceHandle<SpriteResource> sprite;
    float advance = 0.0f;
};

class SpriteFontResource final : public resource::ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept { return "SpriteFontResource"; }

    SpriteFontResource();
    SpriteFontResource(core::pmr::String id, SpriteFontResourceDesc desc);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;

    const SpriteFontResourceDesc& desc() const { return desc_; }
    std::uint32_t maxTextBytes() const { return desc_.maxTextBytes; }
    const core::pmr::Vector<SpriteFontGlyph>& resolvedGlyphs() const { return resolvedGlyphs_; }

private:
    SpriteFontResourceDesc desc_;
    core::pmr::Vector<SpriteFontGlyph> resolvedGlyphs_{&core::defaultCpuMemoryResource()};
};

void registerSpriteFontResourceType();

struct SpriteAnimationFrame {
    core::pmr::String spriteId{&core::defaultCpuMemoryResource()};
    double seconds = 0;
    std::uint32_t eventId = 0;
    resource::ResourceHandle<SpriteResource> sprite;
};

class SpriteAnimationResource final : public resource::ResourceBase {
public:
    SpriteAnimationResource(
        core::pmr::String id, core::pmr::Vector<SpriteAnimationFrame> frames, bool loop);
    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;

    const auto& frames() const { return frames_; }

    bool loop() const { return loop_; }

private:
    core::pmr::Vector<SpriteAnimationFrame> frames_;
    bool loop_;
};
} // namespace arp::scene2d

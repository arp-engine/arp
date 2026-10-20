#include "2d/sprite_resources.h"

#include "core/file_io.h"
#include "core/yaml_contract.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::scene2d {
namespace {
resource::ResourceReference makeReference(core::pmr::String id, const char* type)
{
    return {std::move(id), core::pmr::String(type), {}, "Contract Complete", {}};
}

resource::ResourceLoadResult outcome(
    const resource::ResourceBase& resource, const char* diagnostic = nullptr)
{
    resource::ResourceLoadResult r;
    r.resourceId = resource.reference().id;
    r.resourceType = resource.reference().type;
    r.success = diagnostic == nullptr;
    r.state = r.success ? resource::ResourceState::Ready : resource::ResourceState::Failed;
    if (diagnostic)
        r.diagnostics = diagnostic;
    return r;
}

float number(const core::YamlContract::Node& n, const char* key)
{
    const auto& v = core::YamlContract::required(n, key);
    if (!v.is_integer() && !v.is_float_number())
        core::YamlContract::fail();
    const auto result = v.get_value<float>();
    if (!std::isfinite(result))
        core::YamlContract::fail();
    return result;
}
} // namespace

SpriteAtlasResource::SpriteAtlasResource(core::pmr::String id,
    core::pmr::String metadataPath,
    core::pmr::String pngPath,
    std::size_t maxBytes)
    : ResourceBase(makeReference(std::move(id), "SpriteAtlasResource")),
      metadataPath_(std::move(metadataPath)), pngPath_(std::move(pngPath)), maxBytes_(maxBytes)
{}

bool SpriteAtlasResource::collectFileReadIntents(resource::ResourceManager&,
    core::pmr::Vector<resource::ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic) const
{
    if (!maxBytes_ || metadataPath_.empty() || pngPath_.empty()) {
        diagnostic = "ARP-2D-SOURCE-CONFIG";
        return false;
    }
    for (const auto* p : {&metadataPath_, &pngPath_}) {
        resource::ResourceFileReadIntent intent;
        intent.path = *p;
        intent.role = "sprite-atlas";
        intent.maxBytes = maxBytes_;
        intent.pathScope = core::io::FilePathScope::HostFilesystem;
        intents.push_back(std::move(intent));
    }
    return true;
}

resource::ResourceLoadResult SpriteAtlasResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& c)
{
    if (!maxBytes_ || !resource::isResourceUuid(reference().id))
        return outcome(*this, "ARP-2D-RESOURCE-CONFIG");
    auto metadata = resource::readResourceSource(metadataPath_, c, maxBytes_);
    if (!metadata.success)
        return outcome(*this, metadata.diagnostics.c_str());
    try {
        using Y = core::YamlContract;
        auto n = Y::Node::deserialize(core::pmr::String(
            reinterpret_cast<const char*>(metadata.bytes.data()), metadata.bytes.size()));
        if (Y::text(n, "type") != "arp.sprite-atlas" || Y::uint32(n, "contractVersion") != 1
            || Y::text(n, "resourceId") != reference().id || Y::text(n, "filter") != "nearest"
            || Y::text(n, "alpha") != "straight")
            return outcome(*this, "ARP-2D-ATLAS-CONTRACT");
        const auto width = Y::uint32(n, "width"), height = Y::uint32(n, "height");
        if (!width || !height || std::uint64_t(width) * height > maxBytes_ / 4)
            return outcome(*this, "ARP-2D-ATLAS-BUDGET");
        const auto& sequence = Y::sequence(n, "slices");
        if (sequence.empty() || sequence.size() > std::uint64_t(width) * height)
            return outcome(*this, "ARP-2D-ATLAS-SLICES");
        core::pmr::Vector<SpriteSlice> slices{&core::defaultCpuMemoryResource()};
        for (const auto& node : sequence) {
            SpriteSlice s;
            s.name = Y::text(node, "name");
            s.x = Y::uint32(node, "x");
            s.y = Y::uint32(node, "y");
            s.width = Y::uint32(node, "width");
            s.height = Y::uint32(node, "height");
            s.pivot = {number(node, "pivotX"), number(node, "pivotY")};
            s.pixelsPerUnit = number(node, "pixelsPerUnit");
            if (s.name.empty() || !s.width || !s.height || s.x > width || s.y > height
                || s.width > width - s.x || s.height > height - s.y || s.pixelsPerUnit <= 0
                || s.pivot.x < 0 || s.pivot.x > 1 || s.pivot.y < 0 || s.pivot.y > 1
                || std::any_of(slices.begin(), slices.end(), [&](const auto& v) {
                       return v.name == s.name;
                   }))
                return outcome(*this, "ARP-2D-ATLAS-SLICE-INVALID");
            slices.push_back(std::move(s));
        }
        auto encoded = resource::readResourceSource(pngPath_, c, maxBytes_);
        if (!encoded.success)
            return outcome(*this, encoded.diagnostics.c_str());
        auto decoded = assets::decodeImageToRgba8(
            core::ArrayView<const std::uint8_t>(encoded.bytes.data(), encoded.bytes.size()),
            "image/png",
            Y::boolean(n, "srgb"),
            maxBytes_);
        if (!decoded.success)
            return outcome(*this, decoded.diagnostics.c_str());
        if (decoded.image.width != width || decoded.image.height != height)
            return outcome(*this, "ARP-2D-ATLAS-EXTENT-MISMATCH");
        slices_ = std::move(slices);
        image_ = std::move(decoded.image);
        return manager.admitResourceMetadata(*this, c.policy);
    } catch (...) {
        return outcome(*this, "ARP-2D-ATLAS-YAML-INVALID");
    }
}

const SpriteSlice* SpriteAtlasResource::slice(std::string_view name) const
{
    for (const auto& s : slices_)
        if (std::string_view(s.name) == name)
            return &s;
    return nullptr;
}

SpriteResource::SpriteResource(
    core::pmr::String id, core::pmr::String atlasId, core::pmr::String name)
    : ResourceBase(makeReference(std::move(id), "SpriteResource")), atlasId_(std::move(atlasId)),
      sliceName_(std::move(name))
{
    addDependency({atlasId_, "sprite-atlas"});
}

SpriteFontResource::SpriteFontResource()
    : ResourceBase(makeReference(core::pmr::String(&core::defaultCpuMemoryResource()),
          resourceTypeName()))
{}

SpriteFontResource::SpriteFontResource(core::pmr::String id, SpriteFontResourceDesc desc)
    : ResourceBase(makeReference(std::move(id), resourceTypeName())),
      desc_(std::move(desc))
{}

resource::ResourceLoadResult SpriteFontResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& context)
{
    if (!resource::isResourceUuid(reference().id) || desc_.maxTextBytes == 0u
        || desc_.maxTextBytes > 512u || desc_.glyphs.empty())
        return outcome(*this, "ARP-2D-SPRITE-FONT-CONFIG");

    core::pmr::Vector<SpriteFontGlyph> resolved{&core::defaultCpuMemoryResource()};
    resolved.reserve(desc_.glyphs.size());
    for (std::size_t glyphIndex = 0; glyphIndex < desc_.glyphs.size(); ++glyphIndex) {
        const auto& glyph = desc_.glyphs[glyphIndex];
        if (glyph.codepoint < 32u || glyph.codepoint > 126u
            || !resource::isResourceUuid(glyph.spriteResourceId)
            || !std::isfinite(glyph.advance) || glyph.advance <= 0.0f
            || std::any_of(desc_.glyphs.begin(), desc_.glyphs.begin() + glyphIndex,
                   [&](const auto& previous) {
                       return glyph.codepoint == previous.codepoint;
               }))
            return outcome(*this, "ARP-2D-SPRITE-FONT-GLYPH");

        auto sprite = manager.find<SpriteResource>(glyph.spriteResourceId);
        if (!sprite.ready())
            return outcome(*this, "ARP-2D-SPRITE-FONT-SPRITE-NOT-READY");
        addDependency({glyph.spriteResourceId, "sprite-font-glyph"});
        resolved.push_back({glyph.codepoint, sprite, glyph.advance});
    }

    resolvedGlyphs_ = std::move(resolved);
    return manager.admitResourceMetadata(*this, context.policy);
}

namespace {
core::UniquePtr<resource::ResourceBase> createSpriteFontResource(
    const reflection::object& payload)
{
    auto* typed = payload.cast<SpriteFontResourceDesc*>();
    return typed ? core::makeUnique<SpriteFontResource>(
                       core::pmr::String(&core::defaultCpuMemoryResource()), *typed)
                 : nullptr;
}
} // namespace

void registerSpriteFontResourceType()
{
    auto& factory = resource::prefabResourceFactory();
    if (!factory.supports(SpriteFontResource::resourceTypeName()))
        (void)factory.registerType(SpriteFontResource::resourceTypeName(),
            &createSpriteFontResource);
}

resource::ResourceLoadResult SpriteResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& c)
{
    auto atlas = manager.find<SpriteAtlasResource>(atlasId_);
    if (!atlas.ready() || !atlas.get()->slice(sliceName_))
        return outcome(*this, "ARP-2D-SPRITE-ATLAS-NOT-READY");
    atlas_ = atlas;
    slice_ = atlas.get()->slice(sliceName_);
    return manager.admitResourceMetadata(*this, c.policy);
}

SpriteAnimationResource::SpriteAnimationResource(
    core::pmr::String id, core::pmr::Vector<SpriteAnimationFrame> frames, bool loop)
    : ResourceBase(makeReference(std::move(id), "SpriteAnimationResource")),
      frames_(std::move(frames)), loop_(loop)
{
    for (const auto& f : frames_)
        addDependency({f.spriteId, "animation-frame"});
}

resource::ResourceLoadResult SpriteAnimationResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& c)
{
    if (frames_.empty())
        return outcome(*this, "ARP-2D-ANIMATION-EMPTY");
    for (auto& f : frames_) {
        f.sprite = manager.find<SpriteResource>(f.spriteId);
        if (!std::isfinite(f.seconds) || f.seconds <= 0 || !f.sprite.ready())
            return outcome(*this, "ARP-2D-ANIMATION-FRAME-INVALID");
    }
    return manager.admitResourceMetadata(*this, c.policy);
}
} // namespace arp::scene2d

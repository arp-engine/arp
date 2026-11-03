#include "content.h"

#include "content_bindings.h"
#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::survivors {
using Y = core::YamlContract;

resource::ResourceHandle<scene2d::SpriteResource> Content::sprite(std::string_view name) const
{
    for (const auto& s : sprites_)
        if (std::string_view(s.name) == name)
            return s.handle;
    return {};
}

resource::ResourceHandle<scene2d::SpriteAnimationResource> Content::animation(
    std::string_view name) const
{
    for (const auto& a : animations_)
        if (std::string_view(a.name) == name)
            return a.handle;
    return {};
}

scriptvm::Value Content::spriteIds() const
{
    reflection::hdict values;
    for (const auto& s : sprites_)
        values.emplace(scriptvm::makeString(s.name), scriptvm::makeString(s.handle.id()));
    return scriptvm::Value{std::move(values)};
}

scriptvm::Value Content::animationIds() const
{
    reflection::hdict values;
    for (const auto& a : animations_)
        values.emplace(scriptvm::makeString(a.name), scriptvm::makeString(a.handle.id()));
    return scriptvm::Value{std::move(values)};
}

scriptvm::Value Content::soundIds() const
{
    reflection::hlist values;
    for (const auto& sound : sounds)
        values.push_back(scriptvm::makeString(sound.id()));
    return scriptvm::Value{std::move(values)};
}

core::SharedPtr<const core::pmr::Vector<renderer::ui::SpriteGlyph>> Content::font() const
{
    auto values = core::makeShared<core::pmr::Vector<renderer::ui::SpriteGlyph>>();
    for (unsigned code = 32; code < 128; ++code) {
        char name[32];
        std::snprintf(name, sizeof(name), "glyph_%u", code);
        auto s = sprite(name);
        if (s.ready()) {
            const auto* slice = atlas.get()->slice(s.get()->sliceName());
            values->push_back({code, s, float(slice->width) / slice->pixelsPerUnit});
        }
    }
    return values;
}

namespace {
core::pmr::String contentPath(
    const core::pmr::String& root, std::string_view relative, resource::ResourceFileOrigin origin)
{
    std::filesystem::path path;
    if (!resource::resolveResourceSourcePath(
            std::filesystem::path(root.c_str()), relative, origin, path))
        Y::fail();
    return core::FilePath::utf8(path);
}

bool manifest(const core::pmr::String& root,
    const GameConfig& config,
    const resource::ResourceLoadContext& context,
    resource::ResourceFileOrigin origin,
    Y::Node& output,
    core::pmr::String& diagnostic)
{
    auto source = resource::readResourceText(contentPath(root, ContentBindings::manifest, origin),
        context,
        config.maxSourceBytes,
        &origin);
    if (!source.success) {
        diagnostic = std::move(source.diagnostics);
        return false;
    }
    output = Y::Node::deserialize(source.text);
    if (Y::text(output, "type") != "arp.survivors-content"
        || Y::uint32(output, "contractVersion") != 1)
        Y::fail();
    return true;
}
} // namespace

bool Content::planSourceReads(const resource::ResourceLoadContext& context,
    resource::ResourceFileOrigin origin,
    const GameConfig& config,
    core::pmr::Vector<resource::ResourceFileReadIntent>& reads,
    core::pmr::String& diagnostic)
try {
    if (!resource::appendResourceReadIntent(
            ContentBindings::manifest, config.maxSourceBytes, reads, diagnostic, origin))
        return false;
    if (!context.sources || !context.sources->find(ContentBindings::manifest))
        return true;
    Y::Node n;
    if (!manifest({}, config, context, origin, n, diagnostic))
        return false;
    for (const auto* key : {"atlasMetadata", "atlasImage"})
        if (!resource::appendResourceReadIntent(contentPath({}, Y::text(n, key), origin),
                config.maxTextureBytes,
                reads,
                diagnostic,
                origin))
            return false;
    for (const auto* path :
        {ContentBindings::tileSet, ContentBindings::tileMap, ContentBindings::prefab})
        if (!resource::appendResourceReadIntent(
                path, config.maxSourceBytes, reads, diagnostic, origin))
            return false;
    for (const auto& sound : ContentBindings::sounds)
        if (!resource::appendResourceReadIntent(
                sound.path, config.maxSourceBytes, reads, diagnostic, origin))
            return false;
    return true;
} catch (...) {
    diagnostic = "ARP-SURVIVORS-CONTENT-INVALID";
    return false;
}

bool Content::load(
    const core::pmr::String& root, const GameConfig& config, core::pmr::String& diagnostic)
{
    return loadImpl(root, config, {}, {core::io::FilePathScope::HostFilesystem, {}}, diagnostic);
}

bool Content::loadPrepared(const resource::ResourceLoadContext& context,
    resource::ResourceFileOrigin origin,
    const GameConfig& config,
    core::pmr::String& diagnostic)
{
    if (!context.requirePreloadedSources || !context.sources
        || origin.pathScope != core::io::FilePathScope::MountedResource || !origin.mount) {
        diagnostic = "SURVIVORS-CONTENT-SOURCE-CONTEXT";
        return false;
    }
    return loadImpl({}, config, context, origin, diagnostic);
}

bool Content::loadImpl(const core::pmr::String& root,
    const GameConfig& config,
    const resource::ResourceLoadContext& context,
    resource::ResourceFileOrigin origin,
    core::pmr::String& diagnostic)
{
    try {
        Y::Node n;
        if (!manifest(root, config, context, origin, n, diagnostic))
            return false;
        const auto atlasId = Y::text(n, "atlasId");
        auto result = resources_.load<scene2d::SpriteAtlasResource>(context,
            atlasId,
            contentPath(root, Y::text(n, "atlasMetadata"), origin),
            contentPath(root, Y::text(n, "atlasImage"), origin),
            config.maxTextureBytes,
            origin);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        atlas = resources_.find<scene2d::SpriteAtlasResource>(atlasId);
        for (const auto& node : Y::sequence(n, "sprites")) {
            auto name = Y::text(node, "name"), id = Y::text(node, "resourceId");
            result = resources_.load<scene2d::SpriteResource>(context, id, atlasId, name);
            if (!result.success) {
                diagnostic = result.diagnostics;
                return false;
            }
            sprites_.push_back({name, resources_.find<scene2d::SpriteResource>(id)});
        }
        for (const auto& node : Y::sequence(n, "animations")) {
            auto id = Y::text(node, "resourceId");
            core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
            for (const auto& f : Y::sequence(node, "frames"))
                frames.push_back(
                    {Y::text(f, "spriteId"), Y::number(f, "seconds"), Y::uint32(f, "eventId")});
            result = resources_.load<scene2d::SpriteAnimationResource>(
                context, id, frames, Y::boolean(node, "loop"));
            if (!result.success) {
                diagnostic = result.diagnostics;
                return false;
            }
            animations_.push_back(
                {Y::text(node, "name"), resources_.find<scene2d::SpriteAnimationResource>(id)});
        }
        const auto set = Y::text(n, "tileSetId"), mapId = Y::text(n, "tileMapId");
        result = resources_.load<scene2d::TileSetResource>(context,
            set,
            contentPath(root, ContentBindings::tileSet, origin),
            config.maxSourceBytes,
            origin);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        result = resources_.load<scene2d::TileMapResource>(context,
            mapId,
            set,
            contentPath(root, ContentBindings::tileMap, origin),
            config.maxSourceBytes,
            origin);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        map = resources_.find<scene2d::TileMapResource>(mapId);
        const core::pmr::String prefabId = ContentBindings::prefabId;
        result = resources_.load<scene::ScenePrefabResource>(context,
            prefabId,
            contentPath(root, ContentBindings::prefab, origin),
            config.maxSourceBytes,
            config.maxOrbitBlades + 1,
            origin);
        if (!result.success) {
            diagnostic = result.diagnostics;
            return false;
        }
        orbitPrefab = resources_.find<scene::ScenePrefabResource>(prefabId);
        std::size_t index = 0;
        for (const auto& sound : ContentBindings::sounds) {
            const auto path = contentPath(root, sound.path, origin);
            audio::DecodedAudioPcm decoded;
            if (context.requirePreloadedSources) {
                auto source = resource::readResourceSource(
                    path, context, config.maxSourceBytes, false, &origin);
                if (!source.success) {
                    diagnostic = std::move(source.diagnostics);
                    return false;
                }
                decoded = audio::importAudioMemory({{source.bytes.data(), source.bytes.size()},
                    audio::AudioSourceFormat::Wave,
                    config.maxSourceBytes,
                    config.maxSourceBytes,
                    ContentBindings::maxAudioDecoderBytes});
            } else {
                decoded = audio::importAudioFile(path.c_str(), config.maxSourceBytes);
            }
            if (!decoded.success()) {
                diagnostic = decoded.diagnostics;
                if (context.requirePreloadedSources) {
                    diagnostic = core::pmr::String(decoded.diagnosticId ? decoded.diagnosticId
                                                                        : "SURVIVORS-CONTENT-AUDIO")
                        + ": " + decoded.diagnostics;
                }
                return false;
            }
            audioDecoderPeakBytes_ = std::max(audioDecoderPeakBytes_, decoded.decoderPeakBytes);
            result = resources_.load<resource::AudioClipResource>(
                context, core::pmr::String(sound.id), std::move(decoded.samples), decoded.channels);
            if (!result.success) {
                diagnostic = result.diagnostics;
                return false;
            }
            sounds[index++] = resources_.find<resource::AudioClipResource>(sound.id);
        }
        return true;
    } catch (...) {
        diagnostic = "ARP-SURVIVORS-CONTENT-INVALID";
        return false;
    }
}

std::size_t Content::processedByteCount() const noexcept
{
    std::size_t bytes = atlas.ready() ? atlas.get()->processedByteCount() : 0;
    if (orbitPrefab.ready())
        bytes += orbitPrefab.get()->processedByteCount();
    for (const auto& sound : sounds)
        if (sound.ready())
            bytes += sound.get()->processedByteCount();
    return bytes;
}

} // namespace arp::survivors

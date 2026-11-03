#pragma once
#include "2d/tile_map_runtime.h"
#include "audio/audio_importer.h"
#include "core/encoding/yaml_contract.h"
#include "gameplay_text.h"
#include "resource/audio_clip_resource.h"
#include "scene/scene_asset_resource.h"
#include "scriptvm/runtime/module_registry.h"

#include <array>

namespace arp::survivors {
struct NamedSprite {
    core::pmr::String name;
    resource::ResourceHandle<scene2d::SpriteResource> handle;
};

struct NamedAnimation {
    core::pmr::String name;
    resource::ResourceHandle<scene2d::SpriteAnimationResource> handle;
};

class Content final {
public:
    Content(resource::ResourceManager& resources) : resources_(resources) {}

    bool load(const core::pmr::String& root, const GameConfig&, core::pmr::String& diagnostic);
    bool loadPrepared(const resource::ResourceLoadContext&,
        resource::ResourceFileOrigin,
        const GameConfig&,
        core::pmr::String& diagnostic);
    static bool planSourceReads(const resource::ResourceLoadContext&,
        resource::ResourceFileOrigin,
        const GameConfig&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String& diagnostic);
    std::size_t processedByteCount() const noexcept;

    std::uint64_t audioDecoderPeakBytes() const noexcept { return audioDecoderPeakBytes_; }

    resource::ResourceHandle<scene2d::SpriteResource> sprite(std::string_view) const;
    resource::ResourceHandle<scene2d::SpriteAnimationResource> animation(std::string_view) const;
    scriptvm::Value spriteIds() const;
    scriptvm::Value animationIds() const;
    scriptvm::Value soundIds() const;
    core::SharedPtr<const core::pmr::Vector<renderer::ui::SpriteGlyph>> font() const;
    resource::ResourceHandle<scene2d::SpriteAtlasResource> atlas;
    resource::ResourceHandle<scene2d::TileMapResource> map;
    resource::ResourceHandle<scene::ScenePrefabResource> orbitPrefab;
    std::array<resource::ResourceHandle<resource::AudioClipResource>, 4> sounds;

private:
    bool loadImpl(const core::pmr::String& root,
        const GameConfig&,
        const resource::ResourceLoadContext&,
        resource::ResourceFileOrigin,
        core::pmr::String& diagnostic);
    resource::ResourceManager& resources_;
    core::pmr::Vector<NamedSprite> sprites_;
    core::pmr::Vector<NamedAnimation> animations_;
    std::uint64_t audioDecoderPeakBytes_ = 0;
};
} // namespace arp::survivors

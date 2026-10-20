#pragma once

#include "2d/components.h"
#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"
#include "scene/ids.h"
#include "scene/transform.h"

namespace arp::scene2d {

AENUM()
enum class Scene2DDiagnosticCode {
    None,
    InvalidResidentChunkBudget,
    ResidentChunkBudgetExceeded,
    MissingTileMetadata,
    MissingChunkGrid,
    MissingRendererHandle,
    MissingBounds,
    Perspective2DProjectionUnsupported,
    CameraDoesNotSee2D,
};

ASTRUCT(Construct = Default)

struct Scene2DDiagnostic {
    APROP(Runtime)
    Scene2DDiagnosticCode code = Scene2DDiagnosticCode::None;
    APROP(Runtime)
    arp::core::pmr::String message;
};

AENUM()
enum class Scene2DParticipationSource {
    ComponentDriven,
};

AENUM()
enum class Scene2DRenderItemKind {
    TileMapChunk,
    Sprite,
};

ASTRUCT(Construct = Default)

struct Scene2DRenderItem {
    APROP(Runtime)
    Scene2DRenderItemKind kind = Scene2DRenderItemKind::TileMapChunk;
    APROP(Runtime)
    arp::scene::GameObjectId objectId;
    APROP(Runtime)
    ChunkCoord chunk;
    APROP(Runtime)
    TileMapHandle tileMap;
    APROP(Runtime)
    SpriteHandle sprite;
    APROP(Runtime)
    arp::renderer::MaterialHandle material;
    APROP(Runtime)
    arp::renderer::TextureHandle texture;
    APROP(Runtime)
    Bounds2D worldBounds;
    APROP(Runtime)
    uint64_t sortKey = 0;
};

class Scene2DRenderQueue {
public:
    void addTileMapChunk(Scene2DRenderItem item);
    void addSprite(Scene2DRenderItem item);

    void recordHiddenSprite() { ++hiddenSpriteCount_; }

    const arp::core::pmr::Vector<Scene2DRenderItem>& items() const { return items_; }

    size_t tileMapItemCount() const { return tileMapItemCount_; }

    size_t spriteItemCount() const { return spriteItemCount_; }

    size_t visibleSpriteCount() const { return spriteItemCount_; }

    size_t hiddenSpriteCount() const { return hiddenSpriteCount_; }

    Scene2DParticipationSource participationSource() const
    {
        return Scene2DParticipationSource::ComponentDriven;
    }

    bool usesSceneModeFlag() const { return false; }

    bool exposesNativeBackendHandles() const { return false; }

    size_t nativeBackendHandleCount() const { return 0; }

private:
    arp::core::pmr::Vector<Scene2DRenderItem> items_;
    size_t tileMapItemCount_ = 0;
    size_t spriteItemCount_ = 0;
    size_t hiddenSpriteCount_ = 0;
};

} // namespace arp::scene2d

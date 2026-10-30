#include "editor/viewport/editor_scene_viewport_sprite_extraction.h"

#include "2d/scene_sprite_extractor.h"
#include "2d/tile_map_resource_component.h"
#include "editor/viewport/editor_scene_viewport_tilemap_visibility.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {

bool extractEditorSceneViewportSprites(EditorShell& shell,
    const scene::SceneTraversalSnapshot& visible,
    rhi::Extent2D extent,
    std::uint32_t maxDraws,
    core::pmr::Vector<scene2d::SpriteDrawItem>& spriteItems,
    core::pmr::String& diagnostics)
{
    scene2d::SceneSpriteExtractor spriteExtractor(shell.projectResources(), maxDraws, true);
    if (!spriteExtractor.extract(shell.sceneDocument().scene(), visible)) {
        diagnostics = spriteExtractor.diagnostic();
        return false;
    }
    spriteItems.clear();
    spriteItems.reserve(spriteExtractor.items().size());
    spriteItems.insert(
        spriteItems.end(), spriteExtractor.items().begin(), spriteExtractor.items().end());
    const auto& viewportCamera = shell.sceneViewportCamera();
    const auto visibleWorld = editorSceneViewportTileMapVisibleWorld(
        viewportCamera.viewProjectionMatrix(
            static_cast<float>(extent.width) / static_cast<float>(extent.height)),
        extent.width,
        extent.height,
        0.0f);
    for (const auto& node : visible.nodes()) {
        if (!node.activeInHierarchy)
            continue;
        const auto* object = shell.sceneDocument().scene().findObject(node.objectId);
        if (object == nullptr) {
            diagnostics = "ARP-EDITOR-VIEWPORT-TILEMAP-SNAPSHOT-OBJECT-MISSING";
            return false;
        }
        for (const auto& component : object->components()) {
            if (!component->enabled())
                continue;
            auto* tileMap = dynamic_cast<scene2d::TileMapResourceComponent*>(component.get());
            if (tileMap == nullptr)
                continue;
            if (!visibleWorld.valid()) {
                diagnostics = "ARP-EDITOR-VIEWPORT-TILEMAP-VISIBLE-WORLD-INVALID";
                return false;
            }
            auto* runtime = tileMap->runtime();
            if (runtime == nullptr || !runtime->updateResidency(visibleWorld.worldBounds)
                || !runtime->extract(0.0)) {
                diagnostics = runtime != nullptr && runtime->diagnostic() != nullptr
                    ? runtime->diagnostic()
                    : "ARP-EDITOR-VIEWPORT-TILEMAP-RUNTIME-UNAVAILABLE";
                return false;
            }
            if (runtime->items().size() > maxDraws - spriteItems.size()) {
                diagnostics = "ARP-EDITOR-VIEWPORT-TILEMAP-DRAW-BUDGET-EXCEEDED";
                return false;
            }
            spriteItems.insert(spriteItems.end(), runtime->items().begin(), runtime->items().end());
        }
    }
    return true;
}

} // namespace arp::editor

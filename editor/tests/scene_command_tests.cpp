#include "2d/components.h"
#include "2d/particle_emitter_component.h"
#include "2d/physics_components.h"
#include "2d/screen_canvas_component.h"
#include "2d/sprite_animation_component.h"
#include "2d/sprite_text_component.h"
#include "2d/tile_map_resource_component.h"
#include "3d/animator_component.h"
#include "3d/authoring_components.h"
#include "3d/collider_component.h"
#include "3d/components.h"
#include "audio/audio_components.h"
#include "core/encoding/yaml_contract.h"
#include "editor/editor_shell.h"
#include "editor/viewport/editor_scene_viewport_camera.h"
#include "editor/viewport/editor_scene_viewport_physics_overlay.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_persistence.h"
#include "resource/mesh_asset_resources.h"
#include "resource/prefab_asset_resources.h"
#include "resource/resource_request_component.h"
#include "scene/camera_temporal_component.h"
#include "scene/runtime_components.h"
#include "scene/scene_asset_resource.h"
#include "tests/fixtures/scene_2d_fixture.h"
#include "tests/fixtures/tile_map_fixture.h"
#include "ui/ui.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>

int main()
{
    using namespace arp;
    {
        auto mesh = assets::MeshAsset::debugTriangle();
        auto unreferenced = mesh.vertices.front();
        unreferenced.position[0] = 1000.0f;
        mesh.vertices.push_back(unreferenced);
        assert(mesh.recomputeBounds());
        assert(mesh.bounds.valid && mesh.bounds.max[0] == 0.66f);
        mesh.indices[0] = 3u;
        mesh.vertices[3].position[0] = std::numeric_limits<float>::quiet_NaN();
        assert(!mesh.recomputeBounds() && !mesh.bounds.valid);
    }
    {
        scene::Scene factoryScene(
            core::pmr::String("owner-aware-factory", &core::defaultCpuMemoryResource()));
        scene::GameObject* factoryOwner = nullptr;
        factoryScene.setComponentFactory(
            [&](scene::GameObject& owner,
                const core::pmr::String& type) -> core::UniquePtr<scene::Component> {
                factoryOwner = &owner;
                if (type == "MeshComponent")
                    return core::makeUnique<scene3d::MeshComponent>();
                return core::UniquePtr<scene::Component>{};
            });
        auto& target = factoryScene.createObject(
            core::pmr::String("target", &core::defaultCpuMemoryResource()));
        assert(target.create_component(
                   core::pmr::String("MeshComponent", &core::defaultCpuMemoryResource()))
            != nullptr);
        assert(factoryOwner == &target);
    }
    editor::EditorShell shell;
    const auto root = std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources";
    assert(shell.load(root / "editor.arpui").ok());
    assert(shell.id("workspace_3D").valid());
    shell.activate("workspace_3D");
    assert(shell.workspace() == "3D");
    {
        editor::EditorShell sceneChromeShell;
        assert(sceneChromeShell.load(root / "editor.arpui").ok());
        sceneChromeShell.activate("workspace_3D");
        scene::GameObjectId meshId;
        for (const auto& object : sceneChromeShell.sceneDocument().scene().objects())
            if (object->name() == "MeshInstance")
                meshId = object->id();
        assert(meshId.valid());
        assert(
            sceneChromeShell.submitCommand(editor::SelectSceneObjectCommand{meshId.value()}).status
            == editor::EditorResponseStatus::Completed);
        const auto sceneChrome = sceneChromeShell.drawCommands();
        assert(sceneChrome.ok());
        assert(sceneChromeShell.runtime()
                .canvas()
                .findWidget(sceneChromeShell.id("selected_header"))
                ->visible());
        assert(sceneChromeShell.runtime()
                   .canvas()
                   .findWidget(sceneChromeShell.id("selected_icon"))
                   ->desc()
                   .textureResourceId
            == "icon-cube");
        assert(sceneChromeShell.runtime()
                .canvas()
                .findWidget(sceneChromeShell.id("selected_icon"))
                ->desc()
                .hasTextureSourceRect);
        const auto* selectedMesh = sceneChromeShell.sceneDocument()
                                       .scene()
                                       .findObject(meshId)
                                       ->getComponent<scene3d::MeshComponent>();
        assert(selectedMesh);
        core::pmr::String meshTypeIconName(
            "scene_component_header_", &core::defaultCpuMemoryResource());
        meshTypeIconName += std::to_string(meshId.value());
        meshTypeIconName.push_back('_');
        meshTypeIconName += std::to_string(selectedMesh->id().value());
        meshTypeIconName += "_type_icon";
        const ui::Widget* meshTypeIcon = nullptr;
        for (const auto widgetId : sceneChromeShell.runtime().canvas().traversal()) {
            const auto* widget = sceneChromeShell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == meshTypeIconName)
                meshTypeIcon = widget;
        }
        assert(meshTypeIcon && meshTypeIcon->desc().textureResourceId == "icon-cube");
        assert(meshTypeIcon->desc().hasTextureSourceRect);
        assert(meshTypeIcon->desc().textureSourceRect.valid());
        assert(std::any_of(
            sceneChrome.commands().begin(), sceneChrome.commands().end(), [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Image
                    && command.widgetId != sceneChromeShell.id("viewport")
                    && command.textureResourceId == "icon-cube" && command.hasTextureSourceRect
                    && command.textureSourceRect.valid();
            }));
        for (const auto axis :
            {std::pair{"x", "axis-x"}, std::pair{"y", "axis-y"}, std::pair{"z", "axis-z"}}) {
            assert(std::any_of(sceneChrome.commands().begin(),
                sceneChrome.commands().end(),
                [&](const auto& command) {
                    return command.kind == renderer::ui::UiDrawCommandKind::Text
                        && command.text == axis.first && command.styleId == axis.second;
                }));
        }
        sceneChromeShell.activate("workspace_Game");
        assert(!sceneChromeShell.runtime()
                .canvas()
                .findWidget(sceneChromeShell.id("selected_header"))
                ->visible());
        assert(sceneChromeShell.runtime()
                   .canvas()
                   .findWidget(sceneChromeShell.id("selected_icon"))
                   ->desc()
                   .textureResourceId
            == "icon-probe");
    }
    {
        editor::EditorShell hierarchyShell;
        assert(hierarchyShell.load(root / "editor.arpui").ok());
        hierarchyShell.activate("workspace_3D");
        assert(hierarchyShell.submitCommand(editor::CreateSceneObjectCommand("SearchParent")).status
            == editor::EditorResponseStatus::Completed);
        const auto parentId = hierarchyShell.sceneDocument().selectedObject();
        assert(hierarchyShell.submitCommand(editor::CreateSceneObjectCommand("SearchTarget")).status
            == editor::EditorResponseStatus::Completed);
        const auto targetId = hierarchyShell.sceneDocument().selectedObject();
        assert(hierarchyShell
                   .submitCommand(
                       editor::ReparentSceneObjectCommand{targetId.value(), parentId.value(), 0u})
                   .status
            == editor::EditorResponseStatus::Completed);

        const auto hierarchyLabelVisible = [](const auto& drawPlan, std::string_view label) {
            return std::any_of(
                drawPlan.commands().begin(), drawPlan.commands().end(), [&](const auto& command) {
                    return command.kind == renderer::ui::UiDrawCommandKind::Text
                        && command.rect.x
                        < editor::layout::kSceneHierarchyLeft + editor::layout::kSceneHierarchyWidth
                        && command.rect.y < editor::layout::kSceneHierarchyTop
                            + editor::layout::kSceneHierarchyVisibleHeight
                        && command.rect.y + command.rect.height > editor::layout::kSceneHierarchyTop
                        && command.text == label;
                });
        };
        const auto hierarchyEntries = hierarchyShell.sceneDocument().hierarchyEntries();
        const auto parentEntry =
            std::find_if(hierarchyEntries.begin(), hierarchyEntries.end(), [&](const auto& entry) {
                return entry.objectId == parentId;
            });
        assert(parentEntry != hierarchyEntries.end());
        const auto expandedDraw = hierarchyShell.drawCommands();
        assert(expandedDraw.ok() && hierarchyLabelVisible(expandedDraw, "SearchTarget"));
        const auto visibleNodeIcons = std::count_if(expandedDraw.commands().begin(),
            expandedDraw.commands().end(),
            [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Image
                    && (command.textureResourceId == "icon-node"
                        || command.textureResourceId == "icon-cube"
                        || command.textureResourceId == "icon-world"
                        || command.textureResourceId == "icon-tool-viewport")
                    && command.hasTextureSourceRect && command.textureSourceRect.valid();
            });
        const auto visibleEyeIcons = std::count_if(expandedDraw.commands().begin(),
            expandedDraw.commands().end(),
            [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Image
                    && command.textureResourceId == "icon-eye" && command.hasTextureSourceRect
                    && command.textureSourceRect.valid()
                    && command.clipRect.x == editor::layout::kSceneHierarchyLeft
                    && command.clipRect.y == editor::layout::kSceneHierarchyTop;
            });
        assert(visibleNodeIcons > 0 && visibleEyeIcons == hierarchyEntries.size());
        const auto parentLabel = std::find_if(expandedDraw.commands().begin(),
            expandedDraw.commands().end(),
            [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "SearchParent"
                    && command.rect.x
                    < editor::layout::kSceneHierarchyLeft + editor::layout::kSceneHierarchyWidth;
            });
        assert(parentLabel != expandedDraw.commands().end());
        editor::EditorInputCommand disclosureClick;
        disclosureClick.kind = editor::EditorInputKind::PointerDown;
        disclosureClick.primaryButton = true;
        disclosureClick.x = editor::layout::kSceneHierarchyLeft
            + editor::layout::kSceneHierarchyTextInset
            + static_cast<float>(parentEntry->depth) * editor::layout::kSceneHierarchyDepthIndent
            - 4.5f;
        disclosureClick.y = parentLabel->rect.y + editor::layout::kSceneHierarchyRowHeight * 0.5f;
        assert(hierarchyShell.submitCommand(disclosureClick).status
            == editor::EditorResponseStatus::Completed);
        disclosureClick.kind = editor::EditorInputKind::PointerUp;
        assert(hierarchyShell.submitCommand(disclosureClick).status
            == editor::EditorResponseStatus::Completed);
        assert(hierarchyShell.sceneDocument().selectedObject() == parentId);
        const auto collapsedDraw = hierarchyShell.drawCommands();
        assert(collapsedDraw.ok() && !hierarchyLabelVisible(collapsedDraw, "SearchTarget"));
        assert(std::any_of(collapsedDraw.commands().begin(),
            collapsedDraw.commands().end(),
            [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Image
                    && command.textureResourceId == "icon-chevron-right"
                    && command.hasTextureSourceRect && command.textureSourceRect.valid()
                    && command.clipRect.x == editor::layout::kSceneHierarchyLeft;
            }));

        hierarchyShell.entryChanged(
            core::pmr::String("scene_search", &core::defaultCpuMemoryResource()),
            core::pmr::String("SearchTarget", &core::defaultCpuMemoryResource()));
        const auto searchExpandedDraw = hierarchyShell.drawCommands();
        assert(
            searchExpandedDraw.ok() && hierarchyLabelVisible(searchExpandedDraw, "SearchTarget"));
        hierarchyShell.entryChanged(
            core::pmr::String("scene_search", &core::defaultCpuMemoryResource()),
            core::pmr::String("", &core::defaultCpuMemoryResource()));
        const auto searchClearedDraw = hierarchyShell.drawCommands();
        assert(searchClearedDraw.ok() && !hierarchyLabelVisible(searchClearedDraw, "SearchTarget"));
        const auto collapsedParentLabel = std::find_if(searchClearedDraw.commands().begin(),
            searchClearedDraw.commands().end(),
            [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "SearchParent"
                    && command.rect.x
                    < editor::layout::kSceneHierarchyLeft + editor::layout::kSceneHierarchyWidth;
            });
        assert(collapsedParentLabel != searchClearedDraw.commands().end());
        disclosureClick.kind = editor::EditorInputKind::PointerDown;
        disclosureClick.y =
            collapsedParentLabel->rect.y + editor::layout::kSceneHierarchyRowHeight * 0.5f;
        assert(hierarchyShell.submitCommand(disclosureClick).status
            == editor::EditorResponseStatus::Completed);
        disclosureClick.kind = editor::EditorInputKind::PointerUp;
        assert(hierarchyShell.submitCommand(disclosureClick).status
            == editor::EditorResponseStatus::Completed);
        const auto reexpandedDraw = hierarchyShell.drawCommands();
        assert(reexpandedDraw.ok() && hierarchyLabelVisible(reexpandedDraw, "SearchTarget"));

        hierarchyShell.entryChanged(
            core::pmr::String("scene_search", &core::defaultCpuMemoryResource()),
            core::pmr::String("searchtarget", &core::defaultCpuMemoryResource()));
        const auto filteredDraw = hierarchyShell.drawCommands();
        assert(filteredDraw.ok());
        const auto* rootObject = hierarchyShell.sceneDocument().scene().findObject(
            hierarchyShell.sceneDocument().scene().rootId());
        assert(rootObject);
        const auto hierarchyLabelCount = std::count_if(filteredDraw.commands().begin(),
            filteredDraw.commands().end(),
            [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.rect.x
                    < editor::layout::kSceneHierarchyLeft + editor::layout::kSceneHierarchyWidth
                    && command.rect.y < editor::layout::kSceneHierarchyTop
                        + editor::layout::kSceneHierarchyVisibleHeight
                    && command.rect.y + command.rect.height > editor::layout::kSceneHierarchyTop
                    && (command.text == rootObject->name() || command.text == "SearchParent"
                        || command.text == "SearchTarget");
            });
        assert(hierarchyLabelCount == 3);
        assert(std::any_of(filteredDraw.commands().begin(),
            filteredDraw.commands().end(),
            [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.rect.x
                    < editor::layout::kSceneHierarchyLeft + editor::layout::kSceneHierarchyWidth
                    && command.rect.y < editor::layout::kSceneHierarchyTop
                        + editor::layout::kSceneHierarchyVisibleHeight
                    && command.rect.y + command.rect.height > editor::layout::kSceneHierarchyTop
                    && command.text == rootObject->name();
            }));

        editor::EditorInputCommand filteredClick;
        filteredClick.kind = editor::EditorInputKind::PointerDown;
        filteredClick.primaryButton = true;
        filteredClick.x = editor::layout::kSceneHierarchyLeft + 16.0f;
        filteredClick.y =
            editor::layout::kSceneHierarchyTop + 2.5f * editor::layout::kSceneHierarchyRowHeight;
        assert(hierarchyShell.submitCommand(filteredClick).status
            == editor::EditorResponseStatus::Completed);
        assert(hierarchyShell.sceneDocument().selectedObject() == targetId);
        filteredClick.kind = editor::EditorInputKind::PointerDelta;
        filteredClick.y += editor::layout::kSceneHierarchyRowHeight;
        assert(hierarchyShell.submitCommand(filteredClick).status
            == editor::EditorResponseStatus::Completed);
        filteredClick.kind = editor::EditorInputKind::PointerUp;
        assert(hierarchyShell.submitCommand(filteredClick).status
            == editor::EditorResponseStatus::Completed);
        assert(hierarchyShell.sceneDocument().scene().findObject(targetId)->parentId() == parentId);

        hierarchyShell.activate("tab_import");
        const auto importDraw = hierarchyShell.drawCommands();
        assert(importDraw.ok());
        assert(std::any_of(
            importDraw.commands().begin(), importDraw.commands().end(), [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "No importable resource selected";
            }));
        hierarchyShell.activate("tab_scene");
        hierarchyShell.entryChanged(
            core::pmr::String("scene_search", &core::defaultCpuMemoryResource()),
            core::pmr::String("no-such-node", &core::defaultCpuMemoryResource()));
        const auto emptyDraw = hierarchyShell.drawCommands();
        assert(emptyDraw.ok());
        assert(std::any_of(
            emptyDraw.commands().begin(), emptyDraw.commands().end(), [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "No matching nodes";
            }));
    }
    {
        editor::EditorShell hierarchyScrollShell;
        assert(hierarchyScrollShell.load(root / "editor.arpui").ok());
        hierarchyScrollShell.activate("workspace_3D");
        std::array<scene::GameObjectId, 24> objectIds{};
        for (std::size_t index = 0; index < objectIds.size(); ++index) {
            char objectName[32];
            std::snprintf(objectName, sizeof(objectName), "ScrollNode%02zu", index);
            assert(hierarchyScrollShell.submitCommand(editor::CreateSceneObjectCommand(objectName))
                       .status
                == editor::EditorResponseStatus::Completed);
            objectIds[index] = hierarchyScrollShell.sceneDocument().selectedObject();
        }
        scene::GameObjectId meshInstanceId;
        for (const auto& object : hierarchyScrollShell.sceneDocument().scene().objects())
            if (object->name() == "MeshInstance")
                meshInstanceId = object->id();
        assert(meshInstanceId.valid());

        editor::EditorInputCommand wheel;
        wheel.kind = editor::EditorInputKind::Wheel;
        wheel.x = editor::layout::kSceneHierarchyLeft + 16.0f;
        wheel.y = editor::layout::kSceneHierarchyTop + 40.0f;
        wheel.wheelDelta = -1.0f;
        assert(hierarchyScrollShell.submitCommand(wheel).status
            == editor::EditorResponseStatus::Completed);
        const auto scrolledDraw = hierarchyScrollShell.drawCommands();
        assert(scrolledDraw.ok());
        const float firstVisibleRowY = editor::layout::kSceneHierarchyTop + 3.0f;
        assert(std::any_of(scrolledDraw.commands().begin(),
            scrolledDraw.commands().end(),
            [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.rect.x
                    < editor::layout::kSceneHierarchyLeft + editor::layout::kSceneHierarchyWidth
                    && command.text == "MeshInstance" && command.rect.y >= firstVisibleRowY
                    && command.rect.y < firstVisibleRowY + editor::layout::kSceneHierarchyRowHeight;
            }));
        const float scrollbarX = editor::layout::kSceneHierarchyLeft
            + editor::layout::kSceneHierarchyWidth - editor::layout::kSceneHierarchyScrollbarWidth;
        assert(std::any_of(scrolledDraw.commands().begin(),
            scrolledDraw.commands().end(),
            [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Box
                    && command.rect.x == scrollbarX
                    && command.rect.y == editor::layout::kSceneHierarchyTop;
            }));

        editor::EditorInputCommand click;
        click.kind = editor::EditorInputKind::PointerDown;
        click.primaryButton = true;
        click.x = editor::layout::kSceneHierarchyLeft + 16.0f;
        click.y = editor::layout::kSceneHierarchyTop + 12.0f;
        assert(hierarchyScrollShell.submitCommand(click).status
            == editor::EditorResponseStatus::Completed);
        assert(hierarchyScrollShell.sceneDocument().selectedObject() == meshInstanceId);
        click.kind = editor::EditorInputKind::PointerUp;
        assert(hierarchyScrollShell.submitCommand(click).status
            == editor::EditorResponseStatus::Completed);

        const auto scrollbarThumb = [&](const auto& drawPlan) -> const auto* {
            const auto found = std::find_if(
                drawPlan.commands().begin(), drawPlan.commands().end(), [&](const auto& command) {
                    return command.kind == renderer::ui::UiDrawCommandKind::Box
                        && command.rect.x == scrollbarX
                        && command.rect.height < editor::layout::kSceneHierarchyVisibleHeight
                        && command.rect.height > 0.0f;
                });
            return found == drawPlan.commands().end() ? nullptr : &*found;
        };
        const auto* initialThumb = scrollbarThumb(scrolledDraw);
        assert(initialThumb);
        editor::EditorInputCommand scrollbarInput;
        scrollbarInput.kind = editor::EditorInputKind::PointerDown;
        scrollbarInput.primaryButton = true;
        scrollbarInput.x = scrollbarX + editor::layout::kSceneHierarchyScrollbarWidth * 0.5f;
        scrollbarInput.y = initialThumb->rect.y + initialThumb->rect.height * 0.5f;
        assert(hierarchyScrollShell.submitCommand(scrollbarInput).status
            == editor::EditorResponseStatus::Completed);
        scrollbarInput.kind = editor::EditorInputKind::PointerMoved;
        scrollbarInput.y = editor::layout::kSceneHierarchyTop
            + editor::layout::kSceneHierarchyVisibleHeight - 2.0f;
        assert(hierarchyScrollShell.submitCommand(scrollbarInput).status
            == editor::EditorResponseStatus::Completed);
        scrollbarInput.kind = editor::EditorInputKind::PointerUp;
        assert(hierarchyScrollShell.submitCommand(scrollbarInput).status
            == editor::EditorResponseStatus::Completed);
        assert(hierarchyScrollShell.sceneDocument().selectedObject() == meshInstanceId);
        const auto draggedBottomDraw = hierarchyScrollShell.drawCommands();
        assert(draggedBottomDraw.ok());
        assert(std::any_of(draggedBottomDraw.commands().begin(),
            draggedBottomDraw.commands().end(),
            [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "ScrollNode23";
            }));
        const auto* bottomThumb = scrollbarThumb(draggedBottomDraw);
        assert(bottomThumb);
        const float bottomThumbY = bottomThumb->rect.y;

        scrollbarInput.kind = editor::EditorInputKind::PointerDown;
        scrollbarInput.y = editor::layout::kSceneHierarchyTop + 2.0f;
        assert(hierarchyScrollShell.submitCommand(scrollbarInput).status
            == editor::EditorResponseStatus::Completed);
        scrollbarInput.kind = editor::EditorInputKind::PointerUp;
        assert(hierarchyScrollShell.submitCommand(scrollbarInput).status
            == editor::EditorResponseStatus::Completed);
        const auto pagedUpDraw = hierarchyScrollShell.drawCommands();
        assert(pagedUpDraw.ok());
        const auto* pagedThumb = scrollbarThumb(pagedUpDraw);
        assert(pagedThumb && pagedThumb->rect.y < bottomThumbY);

        wheel.wheelDelta = -1000.0f;
        assert(hierarchyScrollShell.submitCommand(wheel).status
            == editor::EditorResponseStatus::Completed);
        const auto bottomDraw = hierarchyScrollShell.drawCommands();
        assert(bottomDraw.ok());
        assert(std::any_of(
            bottomDraw.commands().begin(), bottomDraw.commands().end(), [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "ScrollNode23";
            }));

        hierarchyScrollShell.entryChanged(
            core::pmr::String("scene_search", &core::defaultCpuMemoryResource()),
            core::pmr::String("ScrollNode00", &core::defaultCpuMemoryResource()));
        const auto filteredScrollDraw = hierarchyScrollShell.drawCommands();
        assert(filteredScrollDraw.ok());
        assert(std::any_of(filteredScrollDraw.commands().begin(),
            filteredScrollDraw.commands().end(),
            [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "ScrollNode00";
            }));
    }
    const auto versionBeforeInvalidCamera = shell.stateVersion();
    editor::SceneViewportCameraCommand invalidCamera;
    invalidCamera.viewportWidth = 100.0f;
    invalidCamera.viewportHeight = 0.0f;
    assert(shell.submitCommand(invalidCamera).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == versionBeforeInvalidCamera);
    {
        editor::EditorSceneViewportCamera camera;
        float screenX = 0.0f, screenY = 0.0f, depth = 0.0f;
        assert(camera.project({0.0f, 0.0f, 0.0f}, 800.0f, 600.0f, screenX, screenY, &depth));
        assert(std::abs(screenX - 400.0f) < 0.01f && std::abs(screenY - 300.0f) < 0.01f);
        assert(depth > 0.0f);
        const auto initialDistance = camera.distance();
        camera.orbit(40.0f, -20.0f);
        assert(std::abs(camera.yawDegrees() - (-25.0f)) < 0.001f);
        assert(std::abs(camera.pitchDegrees() - (-20.0f)) < 0.001f);
        camera.zoom(1.0f);
        assert(camera.distance() < initialDistance);
        const auto targetBeforePan = camera.target();
        camera.pan(20.0f, 10.0f, 600.0f);
        const auto targetAfterPan = camera.target();
        assert(core::distance(targetBeforePan, targetAfterPan) > 0.0f);
        const auto eyeBeforeLook = camera.position();
        camera.look(18.0f, -8.0f);
        assert(core::distance(eyeBeforeLook, camera.position()) < 0.0001f);
        camera.orbitAround(0.0f, 0.0f, {2.0f, 1.0f, -1.0f});
        assert(core::distance(camera.target(), core::Vec3{2.0f, 1.0f, -1.0f}) < 0.0001f);
        const auto eyeBeforeMove = camera.position();
        const auto targetBeforeMove = camera.target();
        camera.move(1.0f, 0.0f);
        assert(core::distance(eyeBeforeMove, camera.position()) > 0.0f);
        assert(core::distance(eyeBeforeMove, camera.position())
            == core::distance(targetBeforeMove, camera.target()));
        const auto expectedRight = core::normalize(core::cross(
            core::Vec3{0.0f, 1.0f, 0.0f}, core::normalize(targetBeforeMove - eyeBeforeMove)));
        const auto moveDelta = camera.target() - targetBeforeMove;
        assert(core::distance(moveDelta,
                   core::scale(expectedRight, std::max(camera.distance() * 0.08f, 0.01f)))
            < 0.0001f);
        camera.orbit(0.0f, 10000.0f);
        assert(camera.pitchDegrees() == -88.0f);
    }
    {
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(shell.id("viewport"));
        assert(viewport);
        const auto initialYaw = shell.sceneViewportCamera().yawDegrees();
        platform::WindowInputEvent down;
        down.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
        down.mouseButton = platform::WindowMouseButton::Middle;
        down.x = viewport->rect.x + viewport->rect.width * 0.5f;
        down.y = viewport->rect.y + viewport->rect.height * 0.5f;
        auto move = down;
        move.kind = platform::WindowInputEvent::Kind::MouseMoved;
        move.dx = 24.0f;
        move.dy = -12.0f;
        auto up = down;
        up.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
        shell.handlePlatformInput(down);
        shell.handlePlatformInput(move);
        shell.handlePlatformInput(up);
        assert(std::abs(shell.sceneViewportCamera().yawDegrees() - initialYaw - 6.0f) < 0.001f);
        const auto* selectedObject =
            shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
        assert(selectedObject);
        assert(core::distance(shell.sceneViewportCamera().target(),
                   selectedObject->world_transform().translation)
            < 0.0001f);
        const auto initialDistance = shell.sceneViewportCamera().distance();
        platform::WindowInputEvent wheel;
        wheel.kind = platform::WindowInputEvent::Kind::MouseWheel;
        wheel.x = down.x;
        wheel.y = down.y;
        wheel.wheelDelta = 1.0f;
        shell.handlePlatformInput(wheel);
        assert(shell.sceneViewportCamera().distance() < initialDistance);
        const auto initialTarget = shell.sceneViewportCamera().target();
        platform::WindowInputEvent shiftDown;
        shiftDown.kind = platform::WindowInputEvent::Kind::KeyDown;
        shiftDown.key = platform::WindowKey::LeftShift;
        platform::WindowInputEvent shiftUp = shiftDown;
        shiftUp.kind = platform::WindowInputEvent::Kind::KeyUp;
        shell.handlePlatformInput(shiftDown);
        shell.handlePlatformInput(down);
        shell.handlePlatformInput(move);
        shell.handlePlatformInput(up);
        shell.handlePlatformInput(shiftUp);
        const auto pannedTarget = shell.sceneViewportCamera().target();
        assert(core::distance(initialTarget, pannedTarget) > 0.0f);

        const auto eyeBeforeLook = shell.sceneViewportCamera().position();
        const auto yawBeforeLook = shell.sceneViewportCamera().yawDegrees();
        auto rightDown = down;
        rightDown.mouseButton = platform::WindowMouseButton::Right;
        auto rightUp = rightDown;
        rightUp.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
        shell.handlePlatformInput(rightDown);
        shell.handlePlatformInput(move);
        shell.handlePlatformInput(rightUp);
        assert(std::abs(shell.sceneViewportCamera().yawDegrees() - yawBeforeLook - 6.0f) < 0.001f);
        assert(core::distance(eyeBeforeLook, shell.sceneViewportCamera().position()) < 0.0001f);

        const auto eyeBeforeFly = shell.sceneViewportCamera().position();
        shell.handlePlatformInput(rightDown);
        platform::WindowInputEvent forward;
        forward.kind = platform::WindowInputEvent::Kind::KeyDown;
        forward.key = platform::WindowKey::W;
        shell.handlePlatformInput(forward);
        shell.handlePlatformInput(rightUp);
        assert(core::distance(eyeBeforeFly, shell.sceneViewportCamera().position()) > 0.0f);
    }
    {
        editor::EditorShell selectionShell;
        assert(selectionShell.load(root / "editor.arpui").ok());
        selectionShell.activate("workspace_3D");
        assert(selectionShell.submitCommand(editor::CreateSceneObjectCommand("BoxSelectA")).status
            == editor::EditorResponseStatus::Completed);
        const auto first = selectionShell.sceneDocument().selectedObject();
        assert(selectionShell.submitCommand(editor::CreateSceneObjectCommand("BoxSelectB")).status
            == editor::EditorResponseStatus::Completed);
        const auto second = selectionShell.sceneDocument().selectedObject();
        auto firstTransform =
            selectionShell.sceneDocument().scene().findObject(first)->local_transform();
        firstTransform.translation = {-0.5f, 0.0f, 0.0f};
        auto secondTransform =
            selectionShell.sceneDocument().scene().findObject(second)->local_transform();
        secondTransform.translation = {0.5f, 0.0f, 0.0f};
        assert(selectionShell
                   .submitCommand(editor::SetSceneObjectTransformCommand{first.value(),
                       firstTransform.translation,
                       firstTransform.rotation,
                       firstTransform.scale})
                   .status
            == editor::EditorResponseStatus::Completed);
        assert(selectionShell
                   .submitCommand(editor::SetSceneObjectTransformCommand{second.value(),
                       secondTransform.translation,
                       secondTransform.rotation,
                       secondTransform.scale})
                   .status
            == editor::EditorResponseStatus::Completed);
        const auto layout = selectionShell.runtime().canvas().resolveLayout();
        const auto* viewport = layout.findWidget(selectionShell.id("viewport"));
        assert(viewport);
        float firstX = 0.0f, firstY = 0.0f, secondX = 0.0f, secondY = 0.0f;
        assert(selectionShell.sceneViewportCamera().project(
            {-0.5f, 0.0f, 0.0f}, viewport->rect.width, viewport->rect.height, firstX, firstY));
        assert(selectionShell.sceneViewportCamera().project(
            {0.5f, 0.0f, 0.0f}, viewport->rect.width, viewport->rect.height, secondX, secondY));
        platform::WindowInputEvent boxDown;
        boxDown.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
        boxDown.mouseButton = platform::WindowMouseButton::Left;
        boxDown.x = viewport->rect.x + std::min(firstX, secondX) - 8.0f;
        boxDown.y = viewport->rect.y + std::min(firstY, secondY) - 8.0f;
        auto boxMove = boxDown;
        boxMove.kind = platform::WindowInputEvent::Kind::MouseMoved;
        boxMove.x = viewport->rect.x + std::max(firstX, secondX) + 8.0f;
        boxMove.y = viewport->rect.y + std::max(firstY, secondY) + 8.0f;
        boxMove.dx = boxMove.x - boxDown.x;
        boxMove.dy = boxMove.y - boxDown.y;
        auto boxUp = boxDown;
        boxUp.kind = platform::WindowInputEvent::Kind::MouseButtonUp;
        boxUp.x = boxMove.x;
        boxUp.y = boxMove.y;
        selectionShell.handlePlatformInput(boxDown);
        selectionShell.handlePlatformInput(boxMove);
        const auto marqueeDraw = selectionShell.drawCommands();
        assert(marqueeDraw.ok());
        assert(std::any_of(
            marqueeDraw.commands().begin(), marqueeDraw.commands().end(), [](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Box
                    && command.color[2] > command.color[0] && command.color[3] > 0.0f
                    && command.rect.width > 20.0f && command.rect.height > 20.0f;
            }));
        selectionShell.handlePlatformInput(boxUp);
        assert(selectionShell.sceneViewportObjectSelected(first));
        assert(selectionShell.sceneViewportObjectSelected(second));
    }
    const auto inspectorContentId = shell.id("inspector_content");
    const auto* sceneNameWidget = shell.runtime().canvas().findWidget(shell.id("scene_name"));
    const auto* sceneComponentAddWidget =
        shell.runtime().canvas().findWidget(shell.id("scene_component_add"));
    const auto* inspectorPropertiesWidget =
        shell.runtime().canvas().findWidget(shell.id("properties"));
    const auto initialInspectorLayout = shell.runtime().canvas().resolveLayout();
    const auto* resolvedInspectorPanel = initialInspectorLayout.findWidget(shell.id("inspector"));
    const auto* resolvedInspectorProperties =
        initialInspectorLayout.findWidget(shell.id("properties"));
    assert(initialInspectorLayout.ok() && resolvedInspectorPanel && resolvedInspectorProperties);
    assert(inspectorPropertiesWidget->parentId() == shell.id("inspector"));
    assert(resolvedInspectorProperties->rect.x >= resolvedInspectorPanel->rect.x);
    assert(resolvedInspectorProperties->rect.x + resolvedInspectorProperties->rect.width
        <= resolvedInspectorPanel->rect.x + resolvedInspectorPanel->rect.width);
    assert(initialInspectorLayout.clipRect(shell.id("properties")).width > 0.0f);
    const auto* bakedInspectorHeader =
        shell.runtime().canvas().findWidget(shell.id("probe_header"));
    assert(inspectorContentId.valid() && sceneNameWidget && sceneComponentAddWidget
        && inspectorPropertiesWidget);
    assert(!shell.runtime().canvas().findWidget(shell.id("property_search"))->visible());
    assert(!shell.runtime().canvas().findWidget(shell.id("property_search_hint"))->visible());
    assert(!shell.runtime().canvas().findWidget(shell.id("property_search_icon"))->visible());
    assert(sceneNameWidget->parentId() == inspectorContentId);
    assert(sceneComponentAddWidget->parentId() == inspectorContentId);
    assert(inspectorPropertiesWidget->visible());
    assert(bakedInspectorHeader && !bakedInspectorHeader->visible());

    const auto initialCount = shell.sceneDocument().scene().objects().size();
    auto created = shell.submitCommand(editor::CreateSceneObjectCommand("CommandObject"));
    assert(created.status == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().objects().size() == initialCount + 1);
    const auto selected = shell.sceneDocument().selectedObject();
    const auto nameBeforeTextCommand = shell.stateVersion();
    const auto genericSceneNameEdit = shell.submitCommand(
        editor::SetTextValueCommand("scene_name", "ShouldNotBypassSceneCommand"));
    assert(genericSceneNameEdit.status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == nameBeforeTextCommand);
    const auto* unchangedNameObject = shell.sceneDocument().scene().findObject(selected);
    assert(unchangedNameObject && unchangedNameObject->name() == "CommandObject");

    shell.activate("tool_1");
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Move);
    {
        const auto currentLayout = shell.runtime().canvas().resolveLayout();
        const auto* viewport = currentLayout.findWidget(shell.id("viewport"));
        const auto* object = shell.sceneDocument().scene().findObject(selected);
        assert(viewport && object);
        const auto origin = object->transform().localTransform().translation;
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        assert(shell.sceneViewportCamera().project(
            origin, viewport->rect.width, viewport->rect.height, x0, y0));
        assert(shell.sceneViewportCamera().project(origin + core::Vec3{1.0f, 0.0f, 0.0f},
            viewport->rect.width,
            viewport->rect.height,
            x1,
            y1));
        const float sx = x1 - x0, sy = y1 - y0;
        editor::EditorInputCommand down;
        down.kind = editor::EditorInputKind::PointerDown;
        down.primaryButton = true;
        down.button = editor::EditorPointerButton::Left;
        down.x = viewport->rect.x + x0 + sx * 0.75f;
        down.y = viewport->rect.y + y0 + sy * 0.75f;
        assert(shell.submitCommand(down).status == editor::EditorResponseStatus::Completed);
        editor::EditorInputCommand drag;
        drag.kind = editor::EditorInputKind::PointerDelta;
        drag.deltaX = sx * 0.1f;
        drag.deltaY = sy * 0.1f;
        assert(shell.submitCommand(drag).status == editor::EditorResponseStatus::Completed);
        const auto* movedObject = shell.sceneDocument().scene().findObject(selected);
        assert(movedObject);
        assert(std::abs(movedObject->transform().localTransform().translation.x - origin.x - 0.1f)
            < 0.001f);
        editor::EditorInputCommand up;
        up.kind = editor::EditorInputKind::PointerUp;
        up.primaryButton = true;
        up.button = editor::EditorPointerButton::Left;
        assert(shell.submitCommand(up).status == editor::EditorResponseStatus::Completed);
    }
    shell.activate("tool_3");
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Scale);
    {
        const auto currentLayout = shell.runtime().canvas().resolveLayout();
        const auto* viewport = currentLayout.findWidget(shell.id("viewport"));
        const auto* object = shell.sceneDocument().scene().findObject(selected);
        assert(viewport && object);
        const auto origin = object->transform().localTransform().translation;
        const float initialScaleX = object->transform().localTransform().scale.x;
        float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
        assert(shell.sceneViewportCamera().project(
            origin, viewport->rect.width, viewport->rect.height, x0, y0));
        assert(shell.sceneViewportCamera().project(origin + core::Vec3{1.0f, 0.0f, 0.0f},
            viewport->rect.width,
            viewport->rect.height,
            x1,
            y1));
        const float sx = x1 - x0, sy = y1 - y0;
        editor::EditorInputCommand down;
        down.kind = editor::EditorInputKind::PointerDown;
        down.primaryButton = true;
        down.button = editor::EditorPointerButton::Left;
        down.x = viewport->rect.x + x0 + sx * 0.75f;
        down.y = viewport->rect.y + y0 + sy * 0.75f;
        assert(shell.submitCommand(down).status == editor::EditorResponseStatus::Completed);
        editor::EditorInputCommand drag;
        drag.kind = editor::EditorInputKind::PointerDelta;
        drag.deltaX = sx * 0.2f;
        drag.deltaY = sy * 0.2f;
        assert(shell.submitCommand(drag).status == editor::EditorResponseStatus::Completed);
        const auto* scaledObject = shell.sceneDocument().scene().findObject(selected);
        assert(scaledObject);
        assert(std::abs(scaledObject->transform().localTransform().scale.x - initialScaleX - 0.2f)
            < 0.001f);
        editor::EditorInputCommand up;
        up.kind = editor::EditorInputKind::PointerUp;
        up.primaryButton = true;
        up.button = editor::EditorPointerButton::Left;
        assert(shell.submitCommand(up).status == editor::EditorResponseStatus::Completed);
    }
    shell.activate("tool_2");
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Rotate);
    {
        const auto currentLayout = shell.runtime().canvas().resolveLayout();
        const auto* viewport = currentLayout.findWidget(shell.id("viewport"));
        const auto* object = shell.sceneDocument().scene().findObject(selected);
        assert(viewport && object);
        const auto before = object->transform().localTransform();
        constexpr float radius = 1.25f;
        constexpr float targetAngle = 0.45f;
        const core::Vec3 ringStart = before.translation + core::Vec3{0.0f, radius, 0.0f};
        const core::Vec3 ringEnd = before.translation
            + core::Vec3{0.0f, radius * std::cos(targetAngle), radius * std::sin(targetAngle)};
        float centerX = 0.0f, centerY = 0.0f;
        float startX = 0.0f, startY = 0.0f, endX = 0.0f, endY = 0.0f;
        assert(shell.sceneViewportCamera().project(
            before.translation, viewport->rect.width, viewport->rect.height, centerX, centerY));
        assert(shell.sceneViewportCamera().project(
            ringStart, viewport->rect.width, viewport->rect.height, startX, startY));
        assert(shell.sceneViewportCamera().project(
            ringEnd, viewport->rect.width, viewport->rect.height, endX, endY));
        editor::EditorInputCommand down;
        down.kind = editor::EditorInputKind::PointerDown;
        down.primaryButton = true;
        down.button = editor::EditorPointerButton::Left;
        down.x = viewport->rect.x + startX;
        down.y = viewport->rect.y + startY;
        assert(shell.submitCommand(down).status == editor::EditorResponseStatus::Completed);
        editor::EditorInputCommand drag;
        drag.kind = editor::EditorInputKind::PointerDelta;
        drag.deltaX = endX - startX;
        drag.deltaY = endY - startY;
        assert(shell.submitCommand(drag).status == editor::EditorResponseStatus::Completed);
        const auto* rotatedObject = shell.sceneDocument().scene().findObject(selected);
        assert(rotatedObject);
        const auto after = rotatedObject->transform().localTransform();
        assert(std::abs(core::dot(after.rotation, before.rotation)) < 0.999f);
        assert(after.translation.x == before.translation.x
            && after.translation.y == before.translation.y
            && after.translation.z == before.translation.z);
        assert(after.scale.x == before.scale.x && after.scale.y == before.scale.y
            && after.scale.z == before.scale.z);
        editor::EditorInputCommand up;
        up.kind = editor::EditorInputKind::PointerUp;
        up.primaryButton = true;
        up.button = editor::EditorPointerButton::Left;
        assert(shell.submitCommand(up).status == editor::EditorResponseStatus::Completed);
    }
    {
        auto parentCreated =
            shell.submitCommand(editor::CreateSceneObjectCommand("HierarchyTransformParent"));
        assert(parentCreated.status == editor::EditorResponseStatus::Completed);
        auto childCreated =
            shell.submitCommand(editor::CreateSceneObjectCommand("HierarchyTransformChild"));
        assert(childCreated.status == editor::EditorResponseStatus::Completed);
        const auto parentId = shell.sceneDocument()
                                  .scene()
                                  .findObjectByName(core::pmr::String("HierarchyTransformParent",
                                      &core::defaultCpuMemoryResource()))
                                  ->id();
        const auto childId = shell.sceneDocument()
                                 .scene()
                                 .findObjectByName(core::pmr::String(
                                     "HierarchyTransformChild", &core::defaultCpuMemoryResource()))
                                 ->id();
        editor::SetSceneObjectTransformCommand parentTransform;
        parentTransform.objectId = parentId.value();
        parentTransform.translation = {1.5f, 0.5f, -1.0f};
        parentTransform.rotation = core::Quat::fromYawPitchRoll(0.65f, 0.0f, 0.0f);
        parentTransform.scale = {2.0f, 3.0f, 0.5f};
        assert(
            shell.submitCommand(parentTransform).status == editor::EditorResponseStatus::Completed);
        editor::SetSceneObjectTransformCommand childTransform;
        childTransform.objectId = childId.value();
        childTransform.translation = {0.25f, 0.4f, -2.0f};
        assert(
            shell.submitCommand(childTransform).status == editor::EditorResponseStatus::Completed);
        assert(shell
                   .submitCommand(
                       editor::ReparentSceneObjectCommand{childId.value(), parentId.value(), 0})
                   .status
            == editor::EditorResponseStatus::Completed);
        shell.activate("tool_4");
        assert(shell.submitCommand(editor::SelectSceneObjectCommand{parentId.value()}).status
            == editor::EditorResponseStatus::Completed);
        const auto currentLayout = shell.runtime().canvas().resolveLayout();
        const auto* viewport = currentLayout.findWidget(shell.id("viewport"));
        auto* child = shell.sceneDocument().scene().findObject(childId);
        assert(viewport && child && child->parentId() == parentId);
        const auto initialWorld = child->world_transform();
        float childScreenX = 0.0f, childScreenY = 0.0f;
        assert(shell.sceneViewportCamera().project(initialWorld.translation,
            viewport->rect.width,
            viewport->rect.height,
            childScreenX,
            childScreenY));
        editor::EditorInputCommand pickChild;
        pickChild.kind = editor::EditorInputKind::PointerDown;
        pickChild.primaryButton = true;
        pickChild.button = editor::EditorPointerButton::Left;
        pickChild.x = viewport->rect.x + childScreenX;
        pickChild.y = viewport->rect.y + childScreenY;
        assert(shell.submitCommand(pickChild).status == editor::EditorResponseStatus::Completed);
        assert(shell.sceneDocument().selectedObject() == childId);
        editor::EditorInputCommand releasePick = pickChild;
        releasePick.kind = editor::EditorInputKind::PointerUp;
        assert(shell.submitCommand(releasePick).status == editor::EditorResponseStatus::Completed);

        shell.activate("tool_1");
        const auto localBeforeDrag = child->transform().localTransform();
        float axisStartX = 0.0f, axisStartY = 0.0f, axisEndX = 0.0f, axisEndY = 0.0f;
        assert(shell.sceneViewportCamera().project(initialWorld.translation,
            viewport->rect.width,
            viewport->rect.height,
            axisStartX,
            axisStartY));
        assert(shell.sceneViewportCamera().project(initialWorld.translation + core::Vec3{1, 0, 0},
            viewport->rect.width,
            viewport->rect.height,
            axisEndX,
            axisEndY));
        const float axisScreenX = axisEndX - axisStartX;
        const float axisScreenY = axisEndY - axisStartY;
        editor::EditorInputCommand beginDrag;
        beginDrag.kind = editor::EditorInputKind::PointerDown;
        beginDrag.primaryButton = true;
        beginDrag.button = editor::EditorPointerButton::Left;
        beginDrag.x = viewport->rect.x + axisStartX + axisScreenX * 0.75f;
        beginDrag.y = viewport->rect.y + axisStartY + axisScreenY * 0.75f;
        assert(shell.submitCommand(beginDrag).status == editor::EditorResponseStatus::Completed);
        editor::EditorInputCommand moveDrag;
        moveDrag.kind = editor::EditorInputKind::PointerDelta;
        moveDrag.deltaX = axisScreenX * 0.1f;
        moveDrag.deltaY = axisScreenY * 0.1f;
        assert(shell.submitCommand(moveDrag).status == editor::EditorResponseStatus::Completed);
        const auto movedWorld = child->world_transform();
        assert(std::abs(movedWorld.translation.x - initialWorld.translation.x - 0.1f) < 0.001f);
        editor::EditorInputCommand endDrag = beginDrag;
        endDrag.kind = editor::EditorInputKind::PointerUp;
        assert(shell.submitCommand(endDrag).status == editor::EditorResponseStatus::Completed);
        assert(shell.submitCommand(editor::UndoSceneCommand{}).status
            == editor::EditorResponseStatus::Completed);
        assert(core::distance(
                   child->transform().localTransform().translation, localBeforeDrag.translation)
            < 0.0001f);
        assert(shell.submitCommand(editor::RedoSceneCommand{}).status
            == editor::EditorResponseStatus::Completed);
        assert(
            std::abs(child->world_transform().translation.x - movedWorld.translation.x) < 0.0001f);
        assert(shell.submitCommand(editor::SelectSceneObjectCommand{selected.value()}).status
            == editor::EditorResponseStatus::Completed);
    }
    shell.activate("tool_4");
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Select);
    const auto sendToolShortcut = [&](editor::EditorInputKey key) {
        editor::EditorInputCommand shortcut;
        shortcut.kind = editor::EditorInputKind::KeyDown;
        shortcut.key = key;
        assert(shell.submitCommand(shortcut).status == editor::EditorResponseStatus::Completed);
    };
    sendToolShortcut(editor::EditorInputKey::W);
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Move);
    sendToolShortcut(editor::EditorInputKey::E);
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Rotate);
    sendToolShortcut(editor::EditorInputKey::R);
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Scale);
    sendToolShortcut(editor::EditorInputKey::Q);
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Select);
    shell.activate("tool_1");
    assert(shell.runtime().requestFocus(shell.id("scene_name")).ok());
    const auto focusedSceneName = shell.runtime().focusedControl();
    const auto* focusedSceneNameWidget = shell.runtime().canvas().findWidget(focusedSceneName);
    assert(focusedSceneNameWidget
        && focusedSceneNameWidget->desc().controlType == ui::UiControlType::TextInput);
    sendToolShortcut(editor::EditorInputKey::E);
    assert(shell.sceneTransformTool() == editor::EditorShell::SceneTransformTool::Move);
    const auto* object = shell.sceneDocument().scene().findObject(selected);
    assert(object && object->name() == "CommandObject");
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(shell.id("scene_name"),
                core::pmr::String("CommandObjectEdited", &core::defaultCpuMemoryResource())))
            .ok());
    object = shell.sceneDocument().scene().findObject(selected);
    assert(object && object->name() == "CommandObjectEdited");

    assert(shell.submitCommand(editor::CreateSceneObjectCommand("HierarchyParent")).status
        == editor::EditorResponseStatus::Completed);
    const auto hierarchyParentId = shell.sceneDocument().selectedObject();
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("HierarchyChild")).status
        == editor::EditorResponseStatus::Completed);
    const auto hierarchyChildId = shell.sceneDocument().selectedObject();
    auto reparentChild = shell.submitCommand(editor::ReparentSceneObjectCommand{
        hierarchyChildId.value(), hierarchyParentId.value(), 0u});
    assert(reparentChild.status == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("HierarchySibling")).status
        == editor::EditorResponseStatus::Completed);
    const auto hierarchySiblingId = shell.sceneDocument().selectedObject();
    const auto preDragHierarchy = shell.sceneDocument().hierarchyEntries();
    const auto childBeforeDrag =
        std::find_if(preDragHierarchy.begin(), preDragHierarchy.end(), [&](const auto& entry) {
            return entry.objectId == hierarchyChildId;
        });
    const auto siblingBeforeDrag =
        std::find_if(preDragHierarchy.begin(), preDragHierarchy.end(), [&](const auto& entry) {
            return entry.objectId == hierarchySiblingId;
        });
    assert(
        childBeforeDrag != preDragHierarchy.end() && siblingBeforeDrag != preDragHierarchy.end());
    const float childRowY = editor::layout::kSceneHierarchyTop
        + static_cast<float>(childBeforeDrag - preDragHierarchy.begin())
            * editor::layout::kSceneHierarchyRowHeight
        + editor::layout::kSceneHierarchyRowHeight * 0.5f;
    const float siblingRowY = editor::layout::kSceneHierarchyTop
        + static_cast<float>(siblingBeforeDrag - preDragHierarchy.begin())
            * editor::layout::kSceneHierarchyRowHeight
        + editor::layout::kSceneHierarchyRowHeight * 0.5f;
    editor::EditorInputCommand hierarchyDown;
    hierarchyDown.kind = editor::EditorInputKind::PointerDown;
    hierarchyDown.primaryButton = true;
    hierarchyDown.button = editor::EditorPointerButton::Left;
    hierarchyDown.x = editor::layout::kSceneHierarchyLeft + 16.0f;
    hierarchyDown.y = childRowY;
    assert(shell.submitCommand(hierarchyDown).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand hierarchyDrag;
    hierarchyDrag.kind = editor::EditorInputKind::PointerDelta;
    hierarchyDrag.deltaY = siblingRowY - childRowY;
    hierarchyDrag.x = hierarchyDown.x;
    hierarchyDrag.y = siblingRowY;
    assert(shell.submitCommand(hierarchyDrag).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand hierarchyUp;
    hierarchyUp.kind = editor::EditorInputKind::PointerUp;
    hierarchyUp.primaryButton = true;
    hierarchyUp.button = editor::EditorPointerButton::Left;
    hierarchyUp.x = hierarchyDown.x;
    hierarchyUp.y = siblingRowY;
    assert(shell.submitCommand(hierarchyUp).status == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(hierarchyChildId)->parentId()
        == hierarchySiblingId);
    assert(shell
               .submitCommand(editor::ReparentSceneObjectCommand{
                   hierarchyChildId.value(), hierarchyParentId.value(), 0u})
               .status
        == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(hierarchyChildId)->parentId()
        == hierarchyParentId);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(hierarchyChildId)->parentId()
        == hierarchySiblingId);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(hierarchyChildId)->parentId()
        == hierarchyParentId);
    assert(shell
               .submitCommand(editor::ReparentSceneObjectCommand{
                   hierarchyChildId.value(), hierarchySiblingId.value(), 0u})
               .status
        == editor::EditorResponseStatus::Completed);

    assert(shell.submitCommand(editor::CreateSceneObjectCommand("HierarchyOrderA")).status
        == editor::EditorResponseStatus::Completed);
    const auto hierarchyOrderA = shell.sceneDocument().selectedObject();
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("HierarchyOrderB")).status
        == editor::EditorResponseStatus::Completed);
    const auto hierarchyOrderB = shell.sceneDocument().selectedObject();
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("HierarchyOrderC")).status
        == editor::EditorResponseStatus::Completed);
    const auto hierarchyOrderC = shell.sceneDocument().selectedObject();
    auto rootChildren = shell.sceneDocument()
                            .scene()
                            .findObject(shell.sceneDocument().scene().rootId())
                            ->childIds();
    assert(rootChildren.size() >= 3u);
    assert(rootChildren[rootChildren.size() - 3u] == hierarchyOrderA);
    assert(rootChildren[rootChildren.size() - 2u] == hierarchyOrderB);
    assert(rootChildren[rootChildren.size() - 1u] == hierarchyOrderC);
    auto hierarchyForOrder = shell.sceneDocument().hierarchyEntries();
    const auto rowCenterY = [&](scene::GameObjectId objectId) {
        const auto row = std::find_if(
            hierarchyForOrder.begin(), hierarchyForOrder.end(), [&](const auto& entry) {
                return entry.objectId == objectId;
            });
        assert(row != hierarchyForOrder.end());
        return editor::layout::kSceneHierarchyTop
            + static_cast<float>(row - hierarchyForOrder.begin())
            * editor::layout::kSceneHierarchyRowHeight
            + editor::layout::kSceneHierarchyRowHeight * 0.5f;
    };
    editor::EditorInputCommand reorderDown = hierarchyDown;
    reorderDown.y = rowCenterY(hierarchyOrderA);
    assert(shell.submitCommand(reorderDown).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand reorderDrag = hierarchyDrag;
    reorderDrag.deltaY = editor::layout::kSceneHierarchyRowHeight;
    reorderDrag.y = rowCenterY(hierarchyOrderB) + editor::layout::kSceneHierarchyRowHeight * 0.3f;
    assert(shell.submitCommand(reorderDrag).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand reorderUp = hierarchyUp;
    reorderUp.y = reorderDrag.y;
    assert(shell.submitCommand(reorderUp).status == editor::EditorResponseStatus::Completed);
    rootChildren = shell.sceneDocument()
                       .scene()
                       .findObject(shell.sceneDocument().scene().rootId())
                       ->childIds();
    assert(rootChildren[rootChildren.size() - 3u] == hierarchyOrderB);
    assert(rootChildren[rootChildren.size() - 2u] == hierarchyOrderA);
    assert(rootChildren[rootChildren.size() - 1u] == hierarchyOrderC);

    hierarchyForOrder = shell.sceneDocument().hierarchyEntries();
    editor::EditorInputCommand cancelDown = hierarchyDown;
    cancelDown.y = rowCenterY(hierarchyOrderC);
    assert(shell.submitCommand(cancelDown).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand cancelDrag = hierarchyDrag;
    cancelDrag.deltaY = -editor::layout::kSceneHierarchyRowHeight;
    cancelDrag.y = rowCenterY(hierarchyOrderB) - editor::layout::kSceneHierarchyRowHeight * 0.3f;
    assert(shell.submitCommand(cancelDrag).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand focusLost;
    focusLost.kind = editor::EditorInputKind::FocusLost;
    assert(shell.submitCommand(focusLost).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand cancelledUp = hierarchyUp;
    cancelledUp.y = cancelDrag.y;
    assert(shell.submitCommand(cancelledUp).status == editor::EditorResponseStatus::Completed);
    rootChildren = shell.sceneDocument()
                       .scene()
                       .findObject(shell.sceneDocument().scene().rootId())
                       ->childIds();
    assert(rootChildren[rootChildren.size() - 3u] == hierarchyOrderB);
    assert(rootChildren[rootChildren.size() - 2u] == hierarchyOrderA);
    assert(rootChildren[rootChildren.size() - 1u] == hierarchyOrderC);

    auto* historyObject = shell.sceneDocument().scene().findObject(hierarchyOrderA);
    assert(historyObject);
    const auto transformBeforeHistory = historyObject->transform().localTransform();
    assert(
        shell.submitCommand(editor::BeginSceneTransformEditCommand{hierarchyOrderA.value()}).status
        == editor::EditorResponseStatus::Completed);
    editor::SetSceneObjectTransformCommand transformStep;
    transformStep.objectId = hierarchyOrderA.value();
    transformStep.translation = {1.0f, 0.0f, 0.0f};
    assert(shell.submitCommand(transformStep).status == editor::EditorResponseStatus::Completed);
    transformStep.translation = {2.0f, 1.0f, 0.0f};
    transformStep.scale = {1.5f, 1.0f, 1.0f};
    assert(shell.submitCommand(transformStep).status == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::EndSceneTransformEditCommand{hierarchyOrderA.value()}).status
        == editor::EditorResponseStatus::Completed);
    const auto transformAfterHistory = historyObject->transform().localTransform();
    assert(
        transformAfterHistory.translation.x == 2.0f && transformAfterHistory.translation.y == 1.0f);
    assert(transformAfterHistory.scale.x == 1.5f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(historyObject->transform().localTransform().translation.x
        == transformBeforeHistory.translation.x);
    assert(historyObject->transform().localTransform().translation.y
        == transformBeforeHistory.translation.y);
    assert(historyObject->transform().localTransform().scale.x == transformBeforeHistory.scale.x);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    rootChildren = shell.sceneDocument()
                       .scene()
                       .findObject(shell.sceneDocument().scene().rootId())
                       ->childIds();
    assert(rootChildren[rootChildren.size() - 3u] == hierarchyOrderA);
    assert(rootChildren[rootChildren.size() - 2u] == hierarchyOrderB);
    assert(rootChildren[rootChildren.size() - 1u] == hierarchyOrderC);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    rootChildren = shell.sceneDocument()
                       .scene()
                       .findObject(shell.sceneDocument().scene().rootId())
                       ->childIds();
    assert(rootChildren[rootChildren.size() - 3u] == hierarchyOrderB);
    assert(rootChildren[rootChildren.size() - 2u] == hierarchyOrderA);
    assert(rootChildren[rootChildren.size() - 1u] == hierarchyOrderC);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(historyObject->transform().localTransform().translation.x
        == transformAfterHistory.translation.x);
    assert(historyObject->transform().localTransform().translation.y
        == transformAfterHistory.translation.y);
    assert(historyObject->transform().localTransform().scale.x == transformAfterHistory.scale.x);
    const auto focusLossStart = historyObject->transform().localTransform();
    assert(
        shell.submitCommand(editor::BeginSceneTransformEditCommand{hierarchyOrderA.value()}).status
        == editor::EditorResponseStatus::Completed);
    transformStep.translation = {9.0f, 8.0f, 7.0f};
    transformStep.scale = {1.0f, 1.0f, 1.0f};
    assert(shell.submitCommand(transformStep).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand transformFocusLost;
    transformFocusLost.kind = editor::EditorInputKind::FocusLost;
    assert(
        shell.submitCommand(transformFocusLost).status == editor::EditorResponseStatus::Completed);
    editor::EditorInputCommand transformPointerUp;
    transformPointerUp.kind = editor::EditorInputKind::PointerUp;
    transformPointerUp.primaryButton = true;
    transformPointerUp.button = editor::EditorPointerButton::Left;
    assert(
        shell.submitCommand(transformPointerUp).status == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(
        historyObject->transform().localTransform().translation.x == focusLossStart.translation.x);
    assert(
        historyObject->transform().localTransform().translation.y == focusLossStart.translation.y);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(historyObject->transform().localTransform().translation.x == 9.0f);
    assert(historyObject->transform().localTransform().translation.y == 8.0f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell
               .submitCommand(editor::ReparentSceneObjectCommand{
                   hierarchyOrderC.value(), shell.sceneDocument().scene().rootId().value(), 0u})
               .status
        == editor::EditorResponseStatus::Completed);
    const auto versionBeforeEmptyRedo = shell.stateVersion();
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.stateVersion() == versionBeforeEmptyRedo);

    const auto hierarchyVersion = shell.stateVersion();
    const auto cycleRejected = shell.submitCommand(editor::ReparentSceneObjectCommand{
        hierarchySiblingId.value(), hierarchyChildId.value(), 0u});
    assert(cycleRejected.status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == hierarchyVersion);
    assert(shell.sceneDocument().scene().findObject(hierarchyChildId)->parentId()
        == hierarchySiblingId);
    const auto hierarchy = shell.sceneDocument().hierarchyEntries();
    assert(hierarchy.size() == shell.sceneDocument().scene().objects().size() + 1u);
    assert(hierarchy.front().objectId == shell.sceneDocument().scene().rootId());
    const auto parentRow = std::find_if(hierarchy.begin(), hierarchy.end(), [&](const auto& entry) {
        return entry.objectId == hierarchyParentId;
    });
    const auto childRow = std::find_if(hierarchy.begin(), hierarchy.end(), [&](const auto& entry) {
        return entry.objectId == hierarchyChildId;
    });
    assert(parentRow != hierarchy.end() && childRow != hierarchy.end());
    const auto siblingRow =
        std::find_if(hierarchy.begin(), hierarchy.end(), [&](const auto& entry) {
            return entry.objectId == hierarchySiblingId;
        });
    assert(siblingRow != hierarchy.end() && childRow == siblingRow + 1);
    assert(childRow->depth == siblingRow->depth + 1u);
    const auto hierarchyDraw = shell.drawCommands();
    assert(hierarchyDraw.ok());
    const renderer::ui::UiDrawCommand* parentLabel = nullptr;
    const renderer::ui::UiDrawCommand* childLabel = nullptr;
    std::size_t parentLabelCount = 0u;
    std::size_t childLabelCount = 0u;
    const auto hierarchyClip = ui::LayoutRect::fromOriginSize(editor::layout::kSceneHierarchyLeft,
        editor::layout::kSceneHierarchyTop,
        editor::layout::kSceneHierarchyWidth,
        editor::layout::kSceneHierarchyVisibleHeight);
    for (const auto& command : hierarchyDraw.commands()) {
        if (command.kind != renderer::ui::UiDrawCommandKind::Text)
            continue;
        const auto& rect = command.rect;
        const bool inHierarchy = rect.x < hierarchyClip.x + hierarchyClip.width
            && rect.x + rect.width > hierarchyClip.x
            && rect.y < hierarchyClip.y + hierarchyClip.height
            && rect.y + rect.height > hierarchyClip.y;
        if (!inHierarchy)
            continue;
        if (command.text == "HierarchyParent") {
            parentLabel = &command;
            ++parentLabelCount;
        }
        if (command.text == "HierarchyChild") {
            childLabel = &command;
            ++childLabelCount;
        }
    }
    assert(parentLabel && childLabel && parentLabelCount == 1u && childLabelCount == 1u
        && childLabel->rect.x > parentLabel->rect.x);
    const auto composedDraw = shell.drawCommands("arp.editor.scene-viewport");
    assert(composedDraw.ok());
    const auto viewportImage = std::find_if(
        composedDraw.commands().begin(), composedDraw.commands().end(), [](const auto& command) {
            return command.kind == renderer::ui::UiDrawCommandKind::Image
                && command.textureSource == renderer::ui::UiTextureSource::External
                && command.textureResourceId == "arp.editor.scene-viewport"
                && command.hasTextureSourceRect;
        });
    assert(viewportImage != composedDraw.commands().end());
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("ViewportOverlapA")).status
        == editor::EditorResponseStatus::Completed);
    const auto viewportOverlapA = shell.sceneDocument().selectedObject();
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("ViewportOverlapB")).status
        == editor::EditorResponseStatus::Completed);
    const auto viewportOverlapB = shell.sceneDocument().selectedObject();
    assert(shell.submitCommand(editor::CreateSceneObjectCommand("ViewportOverlapC")).status
        == editor::EditorResponseStatus::Completed);
    const auto viewportOverlapC = shell.sceneDocument().selectedObject();
    assert(viewportOverlapA != viewportOverlapB);
    assert(viewportOverlapB != viewportOverlapC);
    const auto viewportLabels = shell.drawCommands();
    assert(viewportLabels.ok());
    std::array<const renderer::ui::UiDrawCommand*, 3u> overlapLabels{};
    std::array<const renderer::ui::UiDrawCommand*, 3u> overlapBackings{};
    constexpr std::array<std::string_view, 3u> overlapNames = {
        "ViewportOverlapA", "ViewportOverlapB", "ViewportOverlapC"};
    for (const auto& command : viewportLabels.commands()) {
        if (command.kind != renderer::ui::UiDrawCommandKind::Text
            || command.widgetId != shell.id("viewport"))
            continue;
        const auto found = std::find(overlapNames.begin(), overlapNames.end(), command.text);
        if (found == overlapNames.end())
            continue;
        const auto index = static_cast<std::size_t>(std::distance(overlapNames.begin(), found));
        overlapLabels[index] = &command;
    }
    assert(overlapLabels[0] && overlapLabels[1] && overlapLabels[2]);
    const auto viewportLayout = shell.runtime().canvas().resolveLayout();
    assert(viewportLayout.ok());
    const auto* viewportWidget = viewportLayout.findWidget(shell.id("viewport"));
    assert(viewportWidget);
    const auto viewportLabelCommands = viewportLabels.commands();
    for (std::size_t index = 1u; index < viewportLabelCommands.size(); ++index) {
        const auto& command = viewportLabelCommands[index];
        if (command.kind != renderer::ui::UiDrawCommandKind::Text
            || command.widgetId != shell.id("viewport")
            || std::find(overlapNames.begin(), overlapNames.end(), command.text)
                == overlapNames.end())
            continue;
        const auto& backing = viewportLabelCommands[index - 1u];
        assert(backing.kind == renderer::ui::UiDrawCommandKind::Box);
        assert(std::abs(backing.rect.x - command.rect.x + 3.0f) < 0.001f);
        assert(std::abs(backing.rect.y - command.rect.y + 3.0f) < 0.001f);
        assert(std::abs(backing.rect.width - command.rect.width - 6.0f) < 0.001f);
        assert(std::abs(backing.rect.height - command.rect.height - 6.0f) < 0.001f);
        assert(backing.color[3] == 1.0f);
        const auto found = std::find(overlapNames.begin(), overlapNames.end(), command.text);
        const auto labelIndex =
            static_cast<std::size_t>(std::distance(overlapNames.begin(), found));
        overlapBackings[labelIndex] = &backing;
        assert(backing.rect.x >= viewportWidget->rect.x + 5.0f);
        assert(backing.rect.x + backing.rect.width
                <= viewportWidget->rect.x + viewportWidget->rect.width - 5.0f
            && backing.rect.y >= viewportWidget->rect.y + 37.0f
            && backing.rect.y + backing.rect.height
                <= viewportWidget->rect.y + viewportWidget->rect.height - 33.0f);
    }
    for (std::size_t first = 0u; first < overlapBackings.size(); ++first) {
        assert(overlapBackings[first]);
        for (std::size_t second = first + 1u; second < overlapBackings.size(); ++second) {
            const auto& a = overlapBackings[first]->rect;
            const auto& b = overlapBackings[second]->rect;
            assert(b.x + b.width <= a.x || a.x + a.width <= b.x || b.y + b.height <= a.y
                || a.y + a.height <= b.y);
        }
    }
    assert(overlapLabels[2] < overlapLabels[0] && overlapLabels[2] < overlapLabels[1]);
    platform::WindowInputEvent hierarchyClick;
    hierarchyClick.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
    hierarchyClick.mouseButton = platform::WindowMouseButton::Left;
    hierarchyClick.x = editor::layout::kSceneHierarchyLeft + 12.0f;
    hierarchyClick.y = editor::layout::kSceneHierarchyTop
        + static_cast<float>(std::distance(hierarchy.begin(), childRow))
            * editor::layout::kSceneHierarchyRowHeight
        + editor::layout::kSceneHierarchyRowHeight * 0.5f;
    shell.handlePlatformInput(hierarchyClick);
    assert(shell.sceneDocument().selectedObject() == hierarchyChildId);

    constexpr const char* readyMeshId = "3d573bc0-6c06-41aa-8d58-85f94e8b283a";
    constexpr const char* readyMaterialId = "8643b642-313b-4a29-9dab-94d8d0422f0c";
    constexpr const char* pendingMeshId = "af5b2252-cac2-46f2-a394-e6ee57a45d7a";
    constexpr const char* wrongTypeResourceId = "9a1317d4-8946-46c9-b385-5a2f2e2ed099";
    auto makeReference = [](const char* id, const char* type, const char* displayName) {
        resource::ResourceReference reference;
        reference.id = id;
        reference.type = type;
        reference.status = "Contract Complete";
        reference.displayName = displayName;
        return reference;
    };
    auto readyMesh = core::makeUnique<resource::MeshAssetResource>(
        core::pmr::String(readyMeshId, &core::defaultCpuMemoryResource()),
        assets::MeshAsset::debugTriangle());
    readyMesh->configureMetadata(makeReference(readyMeshId, "MeshAssetResource", "Ready Mesh"));
    assert(shell.projectResources().loadResource(std::move(readyMesh)).success);
    const auto readyMeshHandle = shell.projectResources().find<resource::MeshAssetResource>(
        core::pmr::String(readyMeshId, &core::defaultCpuMemoryResource()));
    assert(readyMeshHandle.ready() && readyMeshHandle.get()->mesh()->bounds.valid);
    assert(readyMeshHandle.get()->mesh()->bounds.min[0] == -0.66f
        && readyMeshHandle.get()->mesh()->bounds.max[0] == 0.66f
        && readyMeshHandle.get()->mesh()->bounds.min[1] == -0.62f
        && readyMeshHandle.get()->mesh()->bounds.max[1] == 0.54f);
    auto readyMaterial = core::makeUnique<resource::MaterialResource>();
    readyMaterial->configureMetadata(
        makeReference(readyMaterialId, "MaterialResource", "Ready Material"));
    assert(shell.projectResources().loadResource(std::move(readyMaterial)).success);
    auto wrongTypeResource = core::makeUnique<resource::MaterialResource>();
    wrongTypeResource->configureMetadata(
        makeReference(wrongTypeResourceId, "MaterialResource", "Wrong Type"));
    assert(shell.projectResources().loadResource(std::move(wrongTypeResource)).success);
    assert(shell.projectResources().declareResource(pendingMeshId, "MeshAssetResource").success);

    const auto* meshObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()));
    assert(meshObject);
    assert(shell.submitCommand(editor::SelectSceneObjectCommand{meshObject->id().value()}).status
        == editor::EditorResponseStatus::Completed);
    const auto* meshComponent = meshObject->getComponent<scene3d::MeshComponent>();
    assert(meshComponent);
    const auto meshProperties = shell.sceneDocument().inspectorProperties(meshObject->id());
    core::pmr::String meshHeaderName("scene_component_header_", &core::defaultCpuMemoryResource());
    meshHeaderName += std::to_string(meshObject->id().value());
    meshHeaderName.push_back('_');
    meshHeaderName += std::to_string(meshComponent->id().value());
    const auto findWidgetByName = [&](const core::pmr::String& name) {
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == name)
                return widget;
        }
        return static_cast<const ui::Widget*>(nullptr);
    };
    ui::WidgetId meshHeaderWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == meshHeaderName)
            meshHeaderWidget = widgetId;
    }
    const auto* meshHeader = shell.runtime().canvas().findWidget(meshHeaderWidget);
    assert(meshHeader && meshHeader->desc().text.empty());
    const auto meshTitleName = meshHeaderName + "_title";
    const auto* meshTitle = findWidgetByName(meshTitleName);
    assert(meshTitle && meshTitle->desc().text == "Mesh"
        && meshTitle->desc().controlType == ui::UiControlType::Label);
    core::pmr::String meshToggleSource(
        "scene_component_toggle_", &core::defaultCpuMemoryResource());
    meshToggleSource += std::to_string(meshObject->id().value());
    meshToggleSource.push_back('_');
    meshToggleSource += std::to_string(meshComponent->id().value());
    core::pmr::String meshEnabledSource("scene_property_", &core::defaultCpuMemoryResource());
    meshEnabledSource += std::to_string(meshObject->id().value());
    meshEnabledSource.push_back('_');
    meshEnabledSource += std::to_string(meshComponent->id().value());
    meshEnabledSource += "_enabled";
    const auto meshDisclosureIconName = meshHeaderName + "_disclosure_icon";
    const auto meshTypeIconName = meshHeaderName + "_type_icon";
    const auto* meshDisclosureIcon = findWidgetByName(meshDisclosureIconName);
    const auto* meshTypeIcon = findWidgetByName(meshTypeIconName);
    assert(
        meshDisclosureIcon && meshDisclosureIcon->desc().textureResourceId == "icon-chevron-down");
    assert(meshTypeIcon && meshTypeIcon->desc().textureResourceId == "icon-cube");
    assert(meshTitle->desc().localRect.x
        >= meshTypeIcon->desc().localRect.x + meshTypeIcon->desc().localRect.width);
    shell.activate(meshToggleSource);
    const auto* collapsedMeshHeader = findWidgetByName(meshHeaderName);
    const auto* collapsedDisclosureIcon = findWidgetByName(meshDisclosureIconName);
    assert(collapsedMeshHeader && !findWidgetByName(meshEnabledSource));
    assert(collapsedMeshHeader->desc().text.empty());
    assert(findWidgetByName(meshTitleName)->desc().text == "Mesh");
    assert(collapsedDisclosureIcon
        && collapsedDisclosureIcon->desc().textureResourceId == "icon-chevron-right");
    shell.activate(meshToggleSource);
    const auto* expandedMeshHeader = findWidgetByName(meshHeaderName);
    const auto* expandedDisclosureIcon = findWidgetByName(meshDisclosureIconName);
    assert(expandedMeshHeader && findWidgetByName(meshEnabledSource));
    assert(expandedMeshHeader->desc().text.empty());
    assert(findWidgetByName(meshTitleName)->desc().text == "Mesh");
    assert(expandedDisclosureIcon
        && expandedDisclosureIcon->desc().textureResourceId == "icon-chevron-down");
    assert(shell.submitCommand(editor::AddSceneComponentCommand("LightComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* removableLight = shell.sceneDocument()
                               .scene()
                               .findObject(meshObject->id())
                               ->getComponent<scene3d::LightComponent>();
    assert(removableLight);
    core::pmr::String removeLightSource(
        "scene_component_remove_", &core::defaultCpuMemoryResource());
    removeLightSource += std::to_string(meshObject->id().value());
    removeLightSource.push_back('_');
    removeLightSource += std::to_string(removableLight->id().value());
    ui::WidgetId removeLightWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == removeLightSource)
            removeLightWidget = widgetId;
    }
    assert(removeLightWidget.valid());
    shell.activate(removeLightSource);
    const auto* remainingMeshObject = shell.sceneDocument().scene().findObject(meshObject->id());
    assert(remainingMeshObject && remainingMeshObject->getComponent<scene3d::MeshComponent>()
        && !remainingMeshObject->getComponent<scene3d::LightComponent>());
    const auto enabledProperty =
        std::find_if(meshProperties.begin(), meshProperties.end(), [](const auto& property) {
            return property.name == "enabled" && property.editable
                && property.kind == editor::SceneInspectorPropertyKind::Boolean;
        });
    assert(std::count_if(meshProperties.begin(), meshProperties.end(), [](const auto& property) {
        return property.name == "enabled";
    }) == 1);
    assert(enabledProperty != meshProperties.end() && enabledProperty->booleanValue
        && enabledProperty->componentId == meshComponent->id().value());
    ui::WidgetId meshEnabledWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == meshEnabledSource)
            meshEnabledWidget = widgetId;
    }
    const auto* meshEnabledControl = shell.runtime().canvas().findWidget(meshEnabledWidget);
    assert(meshEnabledControl
        && meshEnabledControl->desc().controlType == ui::UiControlType::Checkbox);
    core::pmr::String meshEnabledLabelName(meshEnabledSource, &core::defaultCpuMemoryResource());
    meshEnabledLabelName += "_label";
    ui::WidgetId meshEnabledLabelWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == meshEnabledLabelName)
            meshEnabledLabelWidget = widgetId;
    }
    const auto* meshEnabledLabel = shell.runtime().canvas().findWidget(meshEnabledLabelWidget);
    assert(meshEnabledLabel && meshEnabledLabel->desc().text == "enabled");
    assert(shell.runtime().interact(ui::UiControlInteraction::toggle(meshEnabledWidget)).ok());
    assert(!meshComponent->enabled());
    assert(shell.runtime().interact(ui::UiControlInteraction::toggle(meshEnabledWidget)).ok());
    assert(meshComponent->enabled());
    editor::SetSceneComponentPropertyCommand disableMesh("enabled");
    disableMesh.objectId = meshObject->id().value();
    disableMesh.componentId = meshComponent->id().value();
    disableMesh.valueKind = editor::ScenePropertyValueKind::Boolean;
    disableMesh.booleanValue = false;
    assert(shell.submitCommand(disableMesh).status == editor::EditorResponseStatus::Completed);
    assert(!meshComponent->enabled());
    disableMesh.booleanValue = true;
    assert(shell.submitCommand(disableMesh).status == editor::EditorResponseStatus::Completed);
    assert(meshComponent->enabled());
    assert(std::count_if(meshProperties.begin(), meshProperties.end(), [](const auto& property) {
        return (property.name == "resourceId" || property.name == "materialResourceId")
            && property.kind == editor::SceneInspectorPropertyKind::ResourceReference
            && property.editable;
    }) == 2);
    core::pmr::String meshResourceSource("scene_property_", &core::defaultCpuMemoryResource());
    meshResourceSource += std::to_string(meshObject->id().value());
    meshResourceSource.push_back('_');
    meshResourceSource += std::to_string(meshComponent->id().value());
    meshResourceSource += "_resourceId";
    ui::WidgetId meshResourceWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == meshResourceSource)
            meshResourceWidget = widgetId;
    }
    const auto* meshResourceControl = shell.runtime().canvas().findWidget(meshResourceWidget);
    assert(meshResourceControl && meshResourceControl->parentId() == inspectorContentId);
    assert(meshResourceControl->desc().controlType == ui::UiControlType::Dropdown);
    assert(shell.runtime().interact(ui::UiControlInteraction::click(meshResourceWidget)).ok());
    bool hasReadyMeshChoice = false;
    bool hasWrongTypeChoice = false;
    bool hasPendingChoice = false;
    ui::WidgetId readyMeshChoiceWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (!widget || widget->parentId() != shell.runtime().canvas().rootId())
            continue;
        const std::string_view label(widget->desc().text.data(), widget->desc().text.size());
        if (label.find(readyMeshId) != std::string_view::npos) {
            hasReadyMeshChoice = true;
            readyMeshChoiceWidget = widgetId;
        }
        hasWrongTypeChoice = hasWrongTypeChoice
            || label.find(readyMaterialId) != std::string_view::npos
            || label.find(wrongTypeResourceId) != std::string_view::npos;
        hasPendingChoice = hasPendingChoice || label.find(pendingMeshId) != std::string_view::npos;
    }
    assert(hasReadyMeshChoice && !hasWrongTypeChoice && !hasPendingChoice);
    const auto pickerLayout = shell.runtime().canvas().resolveLayout();
    const auto readyChoiceClip = pickerLayout.clipRect(readyMeshChoiceWidget);
    const auto* readyChoiceLayout = pickerLayout.findWidget(readyMeshChoiceWidget);
    assert(pickerLayout.ok() && readyChoiceLayout
        && readyChoiceClip.width >= readyChoiceLayout->rect.width
        && readyChoiceClip.height >= readyChoiceLayout->rect.height);
    assert(shell.runtime().interact(ui::UiControlInteraction::click(meshResourceWidget)).ok());
    editor::SetSceneComponentPropertyCommand forgedResourceId("resourceId");
    forgedResourceId.objectId = meshObject->id().value();
    forgedResourceId.componentId = meshComponent->id().value();
    forgedResourceId.valueKind = editor::ScenePropertyValueKind::Number;
    forgedResourceId.numberValue = 1.0f;
    const auto meshPropertyStateVersion = shell.stateVersion();
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == meshPropertyStateVersion);
    assert(meshComponent->resourceId().empty());
    forgedResourceId.valueKind = editor::ScenePropertyValueKind::ResourceReference;
    forgedResourceId.stringValue = "not-a-resource-id";
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == meshPropertyStateVersion);
    forgedResourceId.stringValue = readyMaterialId;
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == meshPropertyStateVersion);
    forgedResourceId.stringValue = wrongTypeResourceId;
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == meshPropertyStateVersion);
    forgedResourceId.stringValue = pendingMeshId;
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == meshPropertyStateVersion);
    forgedResourceId.stringValue = readyMeshId;
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Completed);
    assert(meshComponent->resourceId() == readyMeshId);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(meshComponent->resourceId().empty());
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(meshComponent->resourceId() == readyMeshId);
    forgedResourceId.stringValue.clear();
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Completed);
    assert(meshComponent->resourceId().empty());
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(meshComponent->resourceId() == readyMeshId);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(meshComponent->resourceId().empty());
    const auto hasSelectedMeshBounds = [](const auto& commands) {
        return std::any_of(commands.begin(), commands.end(), [](const auto& command) {
            return command.kind == renderer::ui::UiDrawCommandKind::Box
                && command.rect.width == 1.5f && command.rect.height == 1.5f
                && std::abs(command.color[0] - 0xe9 / 255.0f) < 0.0001f
                && std::abs(command.color[1] - 0xbb / 255.0f) < 0.0001f
                && std::abs(command.color[2] - 0x63 / 255.0f) < 0.0001f;
        });
    };
    auto emptyResourceDraw = shell.drawCommands();
    assert(emptyResourceDraw.ok() && !hasSelectedMeshBounds(emptyResourceDraw.commands()));
    forgedResourceId.stringValue = readyMeshId;
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Completed);
    auto readyResourceDraw = shell.drawCommands();
    assert(readyResourceDraw.ok() && hasSelectedMeshBounds(readyResourceDraw.commands()));
    forgedResourceId.stringValue.clear();
    assert(shell.submitCommand(forgedResourceId).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setMaterialId("materialResourceId");
    setMaterialId.objectId = meshObject->id().value();
    setMaterialId.componentId = meshComponent->id().value();
    setMaterialId.valueKind = editor::ScenePropertyValueKind::ResourceReference;
    setMaterialId.stringValue = readyMaterialId;
    assert(shell.submitCommand(setMaterialId).status == editor::EditorResponseStatus::Completed);
    assert(meshComponent->materialResourceId() == setMaterialId.stringValue);
    const auto meshColorProperties = shell.sceneDocument().inspectorProperties(meshObject->id());
    assert(std::any_of(
        meshColorProperties.begin(), meshColorProperties.end(), [&](const auto& property) {
            return property.componentId == meshComponent->id().value()
                && property.name == "baseColor" && property.editable
                && property.kind == editor::SceneInspectorPropertyKind::Color4;
        }));
    editor::SetSceneComponentPropertyCommand setMeshColor("baseColor");
    setMeshColor.objectId = meshObject->id().value();
    setMeshColor.componentId = meshComponent->id().value();
    setMeshColor.valueKind = editor::ScenePropertyValueKind::Color4;
    setMeshColor.color4Value = {0.25f, 0.5f, 0.75f, 0.8f};
    assert(shell.submitCommand(setMeshColor).status == editor::EditorResponseStatus::Completed);
    assert(meshComponent->desc().baseColorTint.x == 0.25f
        && meshComponent->desc().baseColorTint.y == 0.5f
        && meshComponent->desc().baseColorTint.z == 0.75f
        && meshComponent->desc().baseColorTint.w == 0.8f);
    const auto meshColorVersion = shell.stateVersion();
    setMeshColor.color4Value.w = std::numeric_limits<float>::infinity();
    assert(shell.submitCommand(setMeshColor).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == meshColorVersion);
    assert(meshComponent->desc().baseColorTint.w == 0.8f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(meshComponent->desc().baseColorTint.x == 1.0f
        && meshComponent->desc().baseColorTint.w == 1.0f);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(meshComponent->desc().baseColorTint.x == 0.25f
        && meshComponent->desc().baseColorTint.w == 0.8f);
    assert(shell.submitCommand(editor::SelectSceneObjectCommand{selected.value()}).status
        == editor::EditorResponseStatus::Completed);

    auto transformed =
        shell.submitCommand(editor::SetSceneObjectTranslationCommand{2.0f, 3.0f, -1.0f});
    assert(transformed.status == editor::EditorResponseStatus::Completed);
    object = shell.sceneDocument().scene().findObject(selected);
    assert(object->transform().localTransform().translation.x == 2.0f);
    assert(object->transform().localTransform().translation.y == 3.0f);
    assert(object->transform().localTransform().translation.z == -1.0f);

    editor::SetSceneObjectTransformCommand fullTransform;
    fullTransform.translation = {4.0f, -2.0f, 7.0f};
    fullTransform.rotation = core::Quat::fromYawPitchRoll(30.0f, 15.0f, -10.0f);
    fullTransform.scale = {2.0f, 0.5f, 3.0f};
    auto transformedFully = shell.submitCommand(fullTransform);
    assert(transformedFully.status == editor::EditorResponseStatus::Completed);
    object = shell.sceneDocument().scene().findObject(selected);
    const auto& full = object->transform().localTransform();
    assert(full.translation.x == 4.0f && full.translation.y == -2.0f && full.translation.z == 7.0f);
    assert(full.scale.x == 2.0f && full.scale.y == 0.5f && full.scale.z == 3.0f);
    assert(std::abs(core::dot(full.rotation, fullTransform.rotation) - 1.0f) < 0.0001f);
    const auto transformProperties = shell.sceneDocument().inspectorProperties(selected);
    const auto findTransformProperty = [&](std::string_view name) {
        return std::find_if(
            transformProperties.begin(), transformProperties.end(), [&](const auto& property) {
                return property.componentType == "TransformComponent" && property.name == name
                    && property.editable
                    && property.kind == editor::SceneInspectorPropertyKind::Vector3;
            });
    };
    const auto positionProperty = findTransformProperty("position");
    const auto rotationProperty = findTransformProperty("rotation");
    const auto scaleProperty = findTransformProperty("scale");
    assert(positionProperty != transformProperties.end()
        && rotationProperty != transformProperties.end()
        && scaleProperty != transformProperties.end());
    assert(positionProperty->componentId
        == object->getComponent<scene::TransformComponent>()->id().value());
    const auto transformWidget = [&](const auto& property, const char* axis) {
        core::pmr::String source("scene_property_", &core::defaultCpuMemoryResource());
        source += std::to_string(property.objectId);
        source.push_back('_');
        source += std::to_string(property.componentId);
        source.push_back('_');
        source += property.name;
        source.push_back('_');
        source += axis;
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == source)
                return widgetId;
        }
        return ui::WidgetId::invalid();
    };
    const auto positionX = transformWidget(*positionProperty, "x");
    assert(positionX.valid());
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(
                positionX, core::pmr::String("5", &core::defaultCpuMemoryResource())))
            .ok());
    object = shell.sceneDocument().scene().findObject(selected);
    assert(object->transform().localTransform().translation.x == 5.0f);
    const auto rotationX = transformWidget(*rotationProperty, "x");
    assert(rotationX.valid());
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(
                rotationX, core::pmr::String("45", &core::defaultCpuMemoryResource())))
            .ok());
    object = shell.sceneDocument().scene().findObject(selected);
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    assert(core::quaternionToYawPitchRollRadians(
        object->transform().localTransform().rotation, yaw, pitch, roll));
    assert(std::abs(pitch - 0.785398f) < 0.0001f);
    const auto scaleY = transformWidget(*scaleProperty, "y");
    assert(scaleY.valid());
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(
                scaleY, core::pmr::String("0.25", &core::defaultCpuMemoryResource())))
            .ok());
    object = shell.sceneDocument().scene().findObject(selected);
    assert(object->transform().localTransform().scale.y == 0.25f);
    const auto stateBeforeInvalidInput = shell.stateVersion();
    const auto scaleX = transformWidget(*scaleProperty, "x");
    assert(scaleX.valid());
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(
                scaleX, core::pmr::String("nan", &core::defaultCpuMemoryResource())))
            .ok());
    assert(shell.stateVersion() == stateBeforeInvalidInput);
    auto inspector = shell.drawCommands();
    assert(inspector.ok());
    for (const auto* property : {&*positionProperty, &*rotationProperty, &*scaleProperty}) {
        for (const char* axis : {"x", "y", "z"}) {
            const auto widgetId = transformWidget(*property, axis);
            const auto* control = shell.runtime().canvas().findWidget(widgetId);
            assert(widgetId.valid() && control && control->visible());
        }
    }
    assert(!shell.id("scene_position_X").valid());
    assert(!shell.id("scene_rotation_X").valid());
    editor::SetSceneComponentPropertyCommand forgedTransformProperty("position");
    forgedTransformProperty.objectId = selected.value();
    forgedTransformProperty.componentId = positionProperty->componentId;
    forgedTransformProperty.valueKind = editor::ScenePropertyValueKind::Vector3;
    forgedTransformProperty.vector3Value = {99.0f, 99.0f, 99.0f};
    const auto stateBeforeForgedTransformWrite = shell.stateVersion();
    assert(shell.submitCommand(forgedTransformProperty).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == stateBeforeForgedTransformWrite);
    const auto preserved = object->transform().localTransform();
    auto moved = shell.submitCommand(editor::SetSceneObjectTranslationCommand{1.0f, 2.0f, 3.0f});
    assert(moved.status == editor::EditorResponseStatus::Completed);
    object = shell.sceneDocument().scene().findObject(selected);
    const auto& afterMove = object->transform().localTransform();
    assert(afterMove.translation.x == 1.0f && afterMove.translation.y == 2.0f
        && afterMove.translation.z == 3.0f);
    assert(afterMove.scale.x == preserved.scale.x && afterMove.scale.y == preserved.scale.y
        && afterMove.scale.z == preserved.scale.z);
    assert(std::abs(core::dot(afterMove.rotation, preserved.rotation) - 1.0f) < 0.0001f);
    auto invalidTransform = fullTransform;
    invalidTransform.rotation = {0.0f, 0.0f, 0.0f, 0.0f};
    const auto priorVersion = shell.stateVersion();
    auto rejectedTransform = shell.submitCommand(invalidTransform);
    assert(rejectedTransform.status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == priorVersion);
    assert(shell.sceneDocument().scene().findObject(selected)->transform().localTransform().scale.x
        == 2.0f);

    const auto componentAddId = shell.id("scene_component_add");
    const auto* componentAddControl = shell.runtime().findControlObject(componentAddId);
    const auto* componentAddWidget = shell.runtime().canvas().findWidget(componentAddId);
    assert(
        componentAddControl && componentAddControl->controlType() == ui::UiControlType::Dropdown);
    assert(componentAddWidget);
    const auto clickWidgetAtPointer = [&](ui::WidgetId widgetId) {
        const auto layout = shell.runtime().canvas().resolveLayout();
        assert(layout.ok());
        const auto* target = layout.findWidget(widgetId);
        assert(target);
        const float x = target->rect.x + target->rect.width * 0.5f;
        const float y = target->rect.y + target->rect.height * 0.5f;
        const auto hit = shell.runtime().canvas().hitTest(layout, {x, y});
        assert(hit.ok() && hit.widgetId() == widgetId);
        assert(shell.runtime().dispatch(ui::UiInputEvent::pointerDown(x, y)).ok());
        assert(shell.runtime().dispatch(ui::UiInputEvent::pointerUp(x, y)).ok());
    };
    clickWidgetAtPointer(componentAddId);
    ui::WidgetId campfireOption;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->desc().text == "Campfire"
            && widget->parentId() == shell.runtime().canvas().rootId())
            campfireOption = widgetId;
    }
    assert(campfireOption.valid());
    clickWidgetAtPointer(campfireOption);
    auto* selectedSceneObject = shell.sceneDocument().scene().findObject(selected);
    assert(selectedSceneObject && selectedSceneObject->getComponent<scene3d::CampfireComponent>());
    assert(selectedSceneObject->components().size() == 2);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.runtime().interact(ui::UiControlInteraction::click(componentAddId)).ok());
    ui::WidgetId documentOption;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->desc().text == "UI Document"
            && widget->parentId() == shell.runtime().canvas().rootId())
            documentOption = widgetId;
    }
    assert(documentOption.valid());
    assert(shell.runtime().interact(ui::UiControlInteraction::click(documentOption)).ok());
    selectedSceneObject = shell.sceneDocument().scene().findObject(selected);
    assert(selectedSceneObject && selectedSceneObject->getComponent<ui::DocumentComponent>());
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.runtime().interact(ui::UiControlInteraction::click(componentAddId)).ok());
    ui::WidgetId particleEmitterOption;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->desc().text == "Particle Emitter 2D"
            && widget->parentId() == shell.runtime().canvas().rootId())
            particleEmitterOption = widgetId;
    }
    assert(particleEmitterOption.valid());
    assert(shell.runtime().interact(ui::UiControlInteraction::click(particleEmitterOption)).ok());
    selectedSceneObject = shell.sceneDocument().scene().findObject(selected);
    assert(selectedSceneObject
        && selectedSceneObject->getComponent<scene2d::ParticleEmitter2DComponent>());
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("DocumentComponent")).status
        == editor::EditorResponseStatus::Completed);
    const auto* documentObject = shell.sceneDocument().scene().findObject(selected);
    const auto* document = documentObject->getComponent<ui::DocumentComponent>();
    assert(document);
    const auto documentProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* field : {"documentSource", "layer"})
        assert(std::any_of(
            documentProperties.begin(), documentProperties.end(), [&](const auto& property) {
                return property.componentId == document->id().value() && property.name == field
                    && property.editable;
            }));

    editor::SetSceneComponentPropertyCommand setDocumentSource("documentSource");
    setDocumentSource.objectId = selected.value();
    setDocumentSource.componentId = document->id().value();
    setDocumentSource.valueKind = editor::ScenePropertyValueKind::String;
    setDocumentSource.stringValue = "ui/hud.arpui";
    assert(
        shell.submitCommand(setDocumentSource).status == editor::EditorResponseStatus::Completed);
    assert(document->documentSource() == "ui/hud.arpui");
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(document->documentSource().empty());
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(document->documentSource() == "ui/hud.arpui");
    const auto documentVersion = shell.stateVersion();
    for (const auto* invalidPath : {"../outside.arpui", "/outside/hud.arpui", "ui/hud.yaml"}) {
        setDocumentSource.stringValue = invalidPath;
        assert(
            shell.submitCommand(setDocumentSource).status == editor::EditorResponseStatus::Failed);
        assert(shell.stateVersion() == documentVersion);
        assert(document->documentSource() == "ui/hud.arpui");
    }
    editor::SetSceneComponentPropertyCommand setDocumentLayer("layer");
    setDocumentLayer.objectId = selected.value();
    setDocumentLayer.componentId = document->id().value();
    setDocumentLayer.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
    setDocumentLayer.unsignedIntegerValue = std::numeric_limits<std::uint32_t>::max();
    assert(shell.submitCommand(setDocumentLayer).status == editor::EditorResponseStatus::Completed);
    assert(document->desc().layer == std::numeric_limits<std::uint32_t>::max());
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("AudioSourceComponent")).status
        == editor::EditorResponseStatus::Completed);
    const auto* audioSource = shell.sceneDocument()
                                  .scene()
                                  .findObject(selected)
                                  ->getComponent<audio::AudioSourceComponent>();
    assert(audioSource && !audioSource->playing());
    auto audioProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* field : {"volume", "loop", "playOnEnable"})
        assert(
            std::any_of(audioProperties.begin(), audioProperties.end(), [&](const auto& property) {
                return property.componentId == audioSource->id().value() && property.name == field
                    && property.editable;
            }));
    editor::SetSceneComponentPropertyCommand setAudioVolume("volume");
    setAudioVolume.objectId = selected.value();
    setAudioVolume.componentId = audioSource->id().value();
    setAudioVolume.valueKind = editor::ScenePropertyValueKind::Number;
    setAudioVolume.numberValue = 0.35f;
    assert(shell.submitCommand(setAudioVolume).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setAudioLoop("loop");
    setAudioLoop.objectId = selected.value();
    setAudioLoop.componentId = audioSource->id().value();
    setAudioLoop.valueKind = editor::ScenePropertyValueKind::Boolean;
    setAudioLoop.booleanValue = true;
    assert(shell.submitCommand(setAudioLoop).status == editor::EditorResponseStatus::Completed);
    const auto audioAuthored = audioSource->editorAuthoringProperties();
    assert(audioAuthored.is<audio::AudioSourceEditorProperties>());
    const auto& audioAuthoredProperties =
        audioAuthored.cast<const audio::AudioSourceEditorProperties&>();
    assert(std::abs(audioAuthoredProperties.volume - 0.35f) < 0.0001f);
    assert(audioAuthoredProperties.loop);
    const auto audioStateVersion = shell.stateVersion();
    setAudioVolume.numberValue = std::numeric_limits<float>::quiet_NaN();
    assert(shell.submitCommand(setAudioVolume).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == audioStateVersion);
    assert(!audioSource->playing());
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    for (const auto& [type, propertyName] : std::array<std::pair<const char*, const char*>, 2>{{
             {"SpriteRendererComponent", "localBounds.extents"},
             {"TileMapComponent", "grid.chunkWorldSize"},
         }}) {
        assert(shell.submitCommand(editor::AddSceneComponentCommand(type)).status
            == editor::EditorResponseStatus::Completed);
        const auto* component =
            shell.sceneDocument().scene().findObject(selected)->components().back().get();
        const auto properties = shell.sceneDocument().inspectorProperties(selected);
        assert(std::any_of(properties.begin(), properties.end(), [&](const auto& property) {
            return property.componentId == component->id().value() && property.name == propertyName
                && property.editable;
        }));
        assert(std::none_of(properties.begin(), properties.end(), [&](const auto& property) {
            return property.componentId == component->id().value()
                && (property.name == "sprite" || property.name == "tileMap"
                    || property.name == "material" || property.name == "texture");
        }));
        core::pmr::String expectedWidgetName("scene_property_", &core::defaultCpuMemoryResource());
        expectedWidgetName += std::to_string(selected.value());
        expectedWidgetName.push_back('_');
        expectedWidgetName += std::to_string(component->id().value());
        expectedWidgetName.push_back('_');
        expectedWidgetName += propertyName;
        if (std::string_view(propertyName) == "localBounds.extents")
            expectedWidgetName += "_x";
        ui::WidgetId propertyWidget;
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == expectedWidgetName)
                propertyWidget = widgetId;
        }
        assert(propertyWidget.valid());
    }

    const auto* sprite = shell.sceneDocument()
                             .scene()
                             .findObject(selected)
                             ->getComponent<scene2d::SpriteRendererComponent>();
    const auto* tileMap = shell.sceneDocument()
                              .scene()
                              .findObject(selected)
                              ->getComponent<scene2d::TileMapComponent>();
    assert(sprite && tileMap);
    auto tileMapProperties = shell.sceneDocument().inspectorProperties(selected);
    assert(std::count_if(tileMapProperties.begin(),
               tileMapProperties.end(),
               [&](const auto& property) {
                   return property.componentId == tileMap->id().value()
                       && property.name == "metadata.tileCountX" && property.editable
                       && property.kind == editor::SceneInspectorPropertyKind::UnsignedInteger;
               })
        == 1);
    core::pmr::String tileCountXSource("scene_property_", &core::defaultCpuMemoryResource());
    tileCountXSource += std::to_string(selected.value());
    tileCountXSource.push_back('_');
    tileCountXSource += std::to_string(tileMap->id().value());
    tileCountXSource += "_metadata.tileCountX";
    ui::WidgetId tileCountXWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == tileCountXSource)
            tileCountXWidget = widgetId;
    }
    assert(tileCountXWidget.valid());
    editor::SetSceneComponentPropertyCommand setTileCountX("metadata.tileCountX");
    setTileCountX.objectId = selected.value();
    setTileCountX.componentId = tileMap->id().value();
    setTileCountX.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
    setTileCountX.unsignedIntegerValue = std::numeric_limits<std::uint32_t>::max();
    assert(shell.submitCommand(setTileCountX).status == editor::EditorResponseStatus::Completed);
    assert(tileMap->desc().metadata.tileCountX == std::numeric_limits<std::uint32_t>::max());
    const auto tileCountStateVersion = shell.stateVersion();
    shell.entryChanged(
        tileCountXSource, core::pmr::String("4294967296", &core::defaultCpuMemoryResource()));
    assert(shell.stateVersion() == tileCountStateVersion);
    assert(tileMap->desc().metadata.tileCountX == std::numeric_limits<std::uint32_t>::max());
    shell.entryChanged(
        tileCountXSource, core::pmr::String("-1", &core::defaultCpuMemoryResource()));
    assert(shell.stateVersion() == tileCountStateVersion);
    assert(tileMap->desc().metadata.tileCountX == std::numeric_limits<std::uint32_t>::max());
    editor::SetSceneComponentPropertyCommand setSpriteExtents("localBounds.extents");
    setSpriteExtents.objectId = selected.value();
    setSpriteExtents.componentId = sprite->id().value();
    setSpriteExtents.valueKind = editor::ScenePropertyValueKind::Vector3;
    setSpriteExtents.vector3Value = {4.0f, 3.0f, 0.0f};
    assert(shell.submitCommand(setSpriteExtents).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setSpriteBoundsExplicit(
        "localBounds.hasExplicitMetadata");
    setSpriteBoundsExplicit.objectId = selected.value();
    setSpriteBoundsExplicit.componentId = sprite->id().value();
    setSpriteBoundsExplicit.valueKind = editor::ScenePropertyValueKind::Boolean;
    setSpriteBoundsExplicit.booleanValue = true;
    assert(shell.submitCommand(setSpriteBoundsExplicit).status
        == editor::EditorResponseStatus::Completed);
    assert(sprite->desc().localBounds.valid());
    editor::SetSceneComponentPropertyCommand setTileMapChunkSize("grid.chunkWorldSize");
    setTileMapChunkSize.objectId = selected.value();
    setTileMapChunkSize.componentId = tileMap->id().value();
    setTileMapChunkSize.valueKind = editor::ScenePropertyValueKind::Number;
    setTileMapChunkSize.numberValue = 64.0f;
    assert(
        shell.submitCommand(setTileMapChunkSize).status == editor::EditorResponseStatus::Completed);
    assert(tileMap->desc().grid.chunkWorldSize == 64.0f);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    selectedSceneObject = shell.sceneDocument().scene().findObject(selected);
    assert(selectedSceneObject && !selectedSceneObject->getComponent<scene3d::CampfireComponent>()
        && selectedSceneObject->components().size() == 1);

    auto added = shell.submitCommand(editor::AddSceneComponentCommand("LightComponent"));
    assert(added.status == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(selected)->components().size() == 2);
    auto removed = shell.submitCommand(editor::RemoveSceneComponentCommand{});
    assert(removed.status == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(selected)->components().size() == 1);
    auto unsupported = shell.submitCommand(editor::AddSceneComponentCommand("UnknownComponent"));
    assert(unsupported.status == editor::EditorResponseStatus::Failed);
    assert(shell.sceneDocument().scene().findObject(selected)->components().size() == 1);
    const auto projectPhysicsProvider = shell.sceneDocument().physicsWorld2DProvider();
    shell.sceneDocument().setPhysicsWorld2DProvider([] {
        return core::SharedPtr<physics2d::PhysicsWorld2D>{};
    });
    const auto physicsUnavailableVersion = shell.stateVersion();
    assert(shell.submitCommand(editor::AddSceneComponentCommand("RigidBody2DComponent")).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == physicsUnavailableVersion);
    shell.sceneDocument().setPhysicsWorld2DProvider(projectPhysicsProvider);
    const auto& componentCatalog = editor::EditorSceneDocument::componentCatalog();
    constexpr const char* unsupportedRuntimeComponents[] = {
        "PlayerInputComponent",
        "CharacterControllerComponent",
        "CameraFollowComponent",
        "WorldAnchorComponent",
        "World2DAnchorComponent",
        "ScreenAnchorComponent",
        "ScriptBehaviorComponent",
        "SceneRuntimeProfileComponent",
        "CameraTemporalComponent",
    };
    const auto unsupportedStateVersion = shell.stateVersion();
    for (const auto* typeName : unsupportedRuntimeComponents) {
        const auto rejected = shell.submitCommand(editor::AddSceneComponentCommand(typeName));
        assert(rejected.status == editor::EditorResponseStatus::Failed);
        assert(shell.stateVersion() == unsupportedStateVersion);
        assert(shell.sceneDocument().scene().findObject(selected)->components().size() == 1);
        assert(
            std::none_of(componentCatalog.begin(), componentCatalog.end(), [&](const auto& entry) {
                return entry.typeName == typeName;
            }));
    }
    {
        auto* owner = shell.sceneDocument().scene().findObject(selected);
        assert(owner);
        auto& temporal = owner->addComponent<scene::CameraTemporalComponent>();
        temporal.set_previous_projection(68.0f, 1.5f, 0.2f, 150.0f);
        temporal.setPreviousPose({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
        temporal.setPoseValid(true);

        const auto properties = shell.sceneDocument().inspectorProperties(selected);
        const auto fov = std::find_if(properties.begin(), properties.end(), [&](const auto& item) {
            return item.componentId == temporal.id().value() && item.name == "previousFovDegrees_";
        });
        const auto poseValid =
            std::find_if(properties.begin(), properties.end(), [&](const auto& item) {
                return item.componentId == temporal.id().value() && item.name == "poseValid_";
            });
        assert(fov != properties.end() && fov->kind == editor::SceneInspectorPropertyKind::Number
            && !fov->editable && fov->numberValue == 68.0f);
        assert(poseValid != properties.end()
            && poseValid->kind == editor::SceneInspectorPropertyKind::Boolean
            && !poseValid->editable && poseValid->booleanValue);

        editor::SetSceneComponentPropertyCommand forbiddenRuntimeWrite("previousFovDegrees_");
        forbiddenRuntimeWrite.objectId = selected.value();
        forbiddenRuntimeWrite.componentId = temporal.id().value();
        forbiddenRuntimeWrite.valueKind = editor::ScenePropertyValueKind::Number;
        forbiddenRuntimeWrite.numberValue = 90.0f;
        const auto stateVersion = shell.stateVersion();
        assert(shell.submitCommand(forbiddenRuntimeWrite).status
            == editor::EditorResponseStatus::Failed);
        assert(shell.stateVersion() == stateVersion && temporal.previousFovDegrees() == 68.0f);
        assert(owner->removeComponent(temporal.id()));
    }

    physics2d::WorldConfig editorPhysicsConfig{
        {0, -9.8f}, 32, 64, 16, 128, 256, 128, 32, 4, 1.f / 60.f};
    auto editorPhysicsWorld = core::makeShared<physics2d::PhysicsWorld2D>(editorPhysicsConfig);
    assert(editorPhysicsWorld->valid());
    shell.sceneDocument().setPhysicsWorld2DProvider([editorPhysicsWorld] {
        return editorPhysicsWorld;
    });
    const auto colliderWithoutBodyVersion = shell.stateVersion();
    assert(shell.submitCommand(editor::AddSceneComponentCommand("Collider2DComponent")).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == colliderWithoutBodyVersion);
    const auto rigidBodyTestTransform =
        shell.sceneDocument().scene().findObject(selected)->transform().localTransform();
    editor::SetSceneObjectTransformCommand resetRigidBodyTestTransform;
    resetRigidBodyTestTransform.translation = rigidBodyTestTransform.translation;
    assert(shell.submitCommand(resetRigidBodyTestTransform).status
        == editor::EditorResponseStatus::Completed);

    for (std::size_t index = 0; index < componentCatalog.size(); ++index) {
        const auto& entry = componentCatalog[index];
        assert(!entry.typeName.empty() && !entry.displayName.empty());
        for (std::size_t otherIndex = index + 1; otherIndex < componentCatalog.size(); ++otherIndex)
            assert(entry.typeName != componentCatalog[otherIndex].typeName);

        const bool needsSpriteRenderer = entry.typeName == "SpriteAnimationComponent";
        const bool needsPhysicsBody = entry.typeName == "Collider2DComponent";
        if (needsSpriteRenderer)
            assert(shell.submitCommand(editor::AddSceneComponentCommand("SpriteRendererComponent"))
                       .status
                == editor::EditorResponseStatus::Completed);
        if (needsPhysicsBody)
            assert(
                shell.submitCommand(editor::AddSceneComponentCommand("RigidBody2DComponent")).status
                == editor::EditorResponseStatus::Completed);
        const auto componentCount =
            shell.sceneDocument().scene().findObject(selected)->components().size();
        const auto catalogAdd =
            shell.submitCommand(editor::AddSceneComponentCommand(entry.typeName));
        if (catalogAdd.status != editor::EditorResponseStatus::Completed)
            std::fprintf(stderr,
                "catalog component creation failed: %.*s\n",
                static_cast<int>(entry.typeName.size()),
                entry.typeName.data());
        assert(catalogAdd.status == editor::EditorResponseStatus::Completed);
        const auto* catalogObject = shell.sceneDocument().scene().findObject(selected);
        assert(catalogObject && catalogObject->components().size() == componentCount + 1);
        assert(std::string_view(catalogObject->components().back()->scriptTypeName())
            == entry.typeName);
        const auto componentId = catalogObject->components().back()->id().value();
        const auto inspector = shell.sceneDocument().inspectorProperties(selected);
        assert(std::any_of(inspector.begin(), inspector.end(), [&](const auto& property) {
            return property.componentId == componentId && property.name == "enabled"
                && property.kind == editor::SceneInspectorPropertyKind::Boolean
                && property.editable;
        }));
        for (const auto& property : inspector) {
            if (property.componentId != componentId)
                continue;
            if (property.kind == editor::SceneInspectorPropertyKind::ResourceReference)
                assert(property.editable && !property.resourceType.empty()
                    && property.resourceDiagnostic.empty());
            if (property.kind == editor::SceneInspectorPropertyKind::Unsupported) {
                std::fprintf(stderr,
                    "unsupported reflected inspector field: %.*s.%.*s (%.*s)\n",
                    static_cast<int>(property.componentType.size()),
                    property.componentType.data(),
                    static_cast<int>(property.name.size()),
                    property.name.data(),
                    static_cast<int>(property.typeName.size()),
                    property.typeName.data());
                assert(false && "catalog Editor metadata must map to a supported Inspector kind");
            }
        }
        assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
            == editor::EditorResponseStatus::Completed);
        catalogObject = shell.sceneDocument().scene().findObject(selected);
        assert(catalogObject && catalogObject->components().size() == componentCount);
        if (needsSpriteRenderer)
            assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
                == editor::EditorResponseStatus::Completed);
        if (needsPhysicsBody)
            assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
                == editor::EditorResponseStatus::Completed);
    }

    {
        const auto target = std::find_if(shell.sceneDocument().scene().objects().begin(),
            shell.sceneDocument().scene().objects().end(),
            [&](const auto& candidate) {
                return candidate->id() != selected;
            });
        assert(target != shell.sceneDocument().scene().objects().end());
        assert(shell.submitCommand(editor::AddSceneComponentCommand("ObjectLinksComponent")).status
            == editor::EditorResponseStatus::Completed);
        auto* owner = shell.sceneDocument().scene().findObject(selected);
        auto* links = owner ? owner->getComponent<scene::ObjectLinksComponent>() : nullptr;
        assert(links);
        const auto properties = shell.sceneDocument().inspectorProperties(selected);
        const auto property =
            std::find_if(properties.begin(), properties.end(), [&](const auto& p) {
                return p.componentId == links->id().value() && p.name == "links"
                    && p.kind == editor::SceneInspectorPropertyKind::SceneObjectLinkList
                    && p.editable;
            });
        assert(property != properties.end());

        editor::SetSceneComponentPropertyCommand setLinks("links");
        setLinks.objectId = selected.value();
        setLinks.componentId = links->id().value();
        setLinks.valueKind = editor::ScenePropertyValueKind::SceneObjectLinkList;
        setLinks.sceneObjectLinkListValue.emplace_back();
        setLinks.sceneObjectLinkListValue.back().name = "spawnPoint";
        setLinks.sceneObjectLinkListValue.back().targetObjectId = (*target)->id().value();
        assert(shell.submitCommand(setLinks).status == editor::EditorResponseStatus::Completed);
        assert(links->links.size() == 1u && links->links.front().name == "spawnPoint"
            && links->links.front().target == (*target)->id());

        auto duplicateLinks = setLinks;
        duplicateLinks.sceneObjectLinkListValue.emplace_back();
        duplicateLinks.sceneObjectLinkListValue.back().name = "spawnPoint";
        duplicateLinks.sceneObjectLinkListValue.back().targetObjectId = (*target)->id().value();
        const auto duplicateVersion = shell.stateVersion();
        assert(shell.submitCommand(duplicateLinks).status == editor::EditorResponseStatus::Failed);
        assert(shell.stateVersion() == duplicateVersion && links->links.size() == 1u);

        assert(shell.submitCommand(editor::UndoSceneCommand{}).status
            == editor::EditorResponseStatus::Completed);
        assert(links->links.empty());
        assert(shell.submitCommand(editor::RedoSceneCommand{}).status
            == editor::EditorResponseStatus::Completed);
        assert(links->links.size() == 1u && links->links.front().target == (*target)->id());

        core::pmr::String yaml{&core::defaultCpuMemoryResource()};
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        assert(editor::EditorSceneAuthoringCodec::encode(shell.sceneDocument(), yaml, diagnostic));
        editor::EditorSceneDocument reloaded(false);
        const auto acceptReferencedResource = [](void*, std::string_view, std::string_view) {
            return true;
        };
        if (!editor::EditorSceneAuthoringCodec::decode(std::string_view(yaml.data(), yaml.size()),
                reloaded,
                acceptReferencedResource,
                nullptr,
                diagnostic))
            std::fprintf(stderr, "ObjectLinks YAML reload failed: %s\n", diagnostic.c_str());
        assert(diagnostic.empty());
        scene::ObjectLinksComponent* loadedLinks = nullptr;
        for (const auto& object : reloaded.scene().objects()) {
            loadedLinks = object->getComponent<scene::ObjectLinksComponent>();
            if (loadedLinks)
                break;
        }
        assert(loadedLinks && loadedLinks->links.size() == 1u
            && loadedLinks->links.front().name == "spawnPoint"
            && reloaded.scene().findObject(loadedLinks->links.front().target));
    }

    {
        auto* tileMapObject = shell.sceneDocument().scene().findObject(selected);
        assert(tileMapObject);
        const auto priorTransform = tileMapObject->transform().localTransform();
        editor::SetSceneObjectTransformCommand identityTransform;
        identityTransform.objectId = selected.value();
        assert(shell.submitCommand(identityTransform).status
            == editor::EditorResponseStatus::Completed);

        resource::ResourceManager primaryTileResources;
        resource::ResourceManager replacementTileResources;
        assert(tests::loadSpriteFixture(primaryTileResources)
            && tests::loadTileFixture(primaryTileResources));
        assert(tests::loadSpriteFixture(replacementTileResources)
            && tests::loadTileFixture(replacementTileResources));
        auto* activeTileResources = &primaryTileResources;
        auto activeTileWorld = editorPhysicsWorld;
        const auto previousResourceProvider = shell.sceneDocument().resourceManagerProvider();
        shell.sceneDocument().setResourceManagerProvider([&] {
            return activeTileResources;
        });
        shell.sceneDocument().setPhysicsWorld2DProvider([&] {
            return activeTileWorld;
        });

        editor::AddSceneComponentCommand addTileMap("TileMapResourceComponent");
        addTileMap.objectId = selected.value();
        editor::SetSceneComponentPropertyCommand mapReference("tileMapResourceId");
        mapReference.valueKind = editor::ScenePropertyValueKind::ResourceReference;
        mapReference.stringValue = tests::tileMapId;
        addTileMap.initialProperties.push_back(std::move(mapReference));
        editor::SetSceneComponentPropertyCommand residentBudget("maxResidentChunks");
        residentBudget.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
        residentBudget.unsignedIntegerValue = 2u;
        addTileMap.initialProperties.push_back(std::move(residentBudget));
        editor::SetSceneComponentPropertyCommand visibleBudget("maxVisibleCells");
        visibleBudget.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
        visibleBudget.unsignedIntegerValue = 12u;
        addTileMap.initialProperties.push_back(std::move(visibleBudget));
        editor::SetSceneComponentPropertyCommand objectIdStart("firstObjectId");
        objectIdStart.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
        objectIdStart.unsignedIntegerValue = 100u;
        addTileMap.initialProperties.push_back(std::move(objectIdStart));
        assert(shell.submitCommand(addTileMap).status == editor::EditorResponseStatus::Completed);

        tileMapObject = shell.sceneDocument().scene().findObject(selected);
        auto* tileMap = tileMapObject->getComponent<scene2d::TileMapResourceComponent>();
        assert(tileMap && tileMap->runtime() && tileMap->map().ready()
            && tileMap->desc().maxResidentChunks == 2u && tileMap->desc().maxVisibleCells == 12u
            && tileMap->desc().firstObjectId == 100u);
        assert(tileMap->runtime()->updateResidency({-2.0f, -1.0f, 4.0f, 2.0f})
            && tileMap->runtime()->residentCount() == 2u);
        assert(activeTileWorld->telemetry().bodies == 2u);

        const auto tileMapComponentId = tileMap->id().value();
        editor::SetSceneComponentPropertyCommand lowerTileMapBudget("maxResidentChunks");
        lowerTileMapBudget.objectId = selected.value();
        lowerTileMapBudget.componentId = tileMapComponentId;
        lowerTileMapBudget.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
        lowerTileMapBudget.unsignedIntegerValue = 1u;
        assert(shell.submitCommand(lowerTileMapBudget).status
            == editor::EditorResponseStatus::Completed);
        tileMapObject = shell.sceneDocument().scene().findObject(selected);
        tileMap = tileMapObject->getComponent<scene2d::TileMapResourceComponent>();
        assert(tileMap && tileMap->runtime() && tileMap->desc().maxResidentChunks == 1u
            && activeTileWorld->telemetry().bodies == 0u);

        const auto tileMapVersion = shell.stateVersion();
        editor::SetSceneComponentPropertyCommand outOfRangeBudget("maxResidentChunks");
        outOfRangeBudget.objectId = selected.value();
        outOfRangeBudget.componentId = tileMap->id().value();
        outOfRangeBudget.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
        outOfRangeBudget.unsignedIntegerValue = scene2d::kEditorTileMapMaxResidentChunks + 1u;
        assert(
            shell.submitCommand(outOfRangeBudget).status == editor::EditorResponseStatus::Failed);
        assert(shell.stateVersion() == tileMapVersion && tileMap->desc().maxResidentChunks == 1u);
        assert(shell.submitCommand(editor::UndoSceneCommand{}).status
            == editor::EditorResponseStatus::Completed);
        tileMapObject = shell.sceneDocument().scene().findObject(selected);
        tileMap = tileMapObject->getComponent<scene2d::TileMapResourceComponent>();
        assert(tileMap && tileMap->runtime() && tileMap->desc().maxResidentChunks == 2u);
        assert(shell.submitCommand(editor::UndoSceneCommand{}).status
            == editor::EditorResponseStatus::Completed);
        assert(!shell.sceneDocument()
                   .scene()
                   .findObject(selected)
                   ->getComponent<scene2d::TileMapResourceComponent>()
            && activeTileWorld->telemetry().bodies == 0u);
        assert(shell.submitCommand(editor::RedoSceneCommand{}).status
            == editor::EditorResponseStatus::Completed);
        tileMap = shell.sceneDocument()
                      .scene()
                      .findObject(selected)
                      ->getComponent<scene2d::TileMapResourceComponent>();
        assert(tileMap && tileMap->runtime() && tileMap->desc().maxResidentChunks == 2u);

        auto replacementTileWorld =
            core::makeShared<physics2d::PhysicsWorld2D>(editorPhysicsConfig);
        activeTileWorld = replacementTileWorld;
        tileMap->onUpdate(scene::SceneUpdateContext{});
        assert(
            tileMap->runtime() && tileMap->runtime()->updateResidency({-2.0f, -1.0f, 4.0f, 2.0f}));
        assert(replacementTileWorld->telemetry().bodies == 2u);
        activeTileResources = &replacementTileResources;
        tileMap->onUpdate(scene::SceneUpdateContext{});
        assert(tileMap->runtime() && tileMap->map().ready()
            && editorPhysicsWorld->telemetry().bodies == 0u
            && replacementTileWorld->telemetry().bodies == 0u);
        assert(tileMap->runtime()->updateResidency({-2.0f, -1.0f, 4.0f, 2.0f})
            && replacementTileWorld->telemetry().bodies == 2u);

        editor::EditorSceneDocument tileMapAuthoring(false);
        tileMapAuthoring.setResourceManagerProvider([&] {
            return activeTileResources;
        });
        tileMapAuthoring.setPhysicsWorld2DProvider([&] {
            return activeTileWorld;
        });
        const auto authoringObjectId = tileMapAuthoring.createObject("TileMap owner");
        auto* authoringComponent = dynamic_cast<scene2d::TileMapResourceComponent*>(
            tileMapAuthoring.addComponent("TileMapResourceComponent", authoringObjectId));
        assert(authoringComponent);
        scene2d::TileMapResourceComponentEditorProperties authoringProperties;
        authoringProperties.tileMapResourceId = tests::tileMapId;
        authoringProperties.maxResidentChunks = 2u;
        authoringProperties.maxVisibleCells = 12u;
        authoringProperties.firstObjectId = 100u;
        assert(authoringComponent->applyEditorAuthoringProperties(
            reflection::object(std::move(authoringProperties))));
        core::pmr::String tileMapYaml, tileMapDiagnostic;
        assert(editor::EditorSceneAuthoringCodec::encode(
            tileMapAuthoring, tileMapYaml, tileMapDiagnostic));
        editor::EditorSceneDocument tileMapRoundTrip(false);
        tileMapRoundTrip.setResourceManagerProvider([&] {
            return activeTileResources;
        });
        tileMapRoundTrip.setPhysicsWorld2DProvider([&] {
            return activeTileWorld;
        });
        const auto resourceValidator =
            [](void* context, std::string_view resourceType, std::string_view resourceId) {
                if (resourceType != "TileMapResource")
                    return false;
                auto* manager = static_cast<resource::ResourceManager*>(context);
                return manager && manager->find<scene2d::TileMapResource>(resourceId).ready();
            };
        assert(editor::EditorSceneAuthoringCodec::decode(
            std::string_view(tileMapYaml.data(), tileMapYaml.size()),
            tileMapRoundTrip,
            resourceValidator,
            activeTileResources,
            tileMapDiagnostic));
        bool foundRoundTripTileMap = false;
        for (const auto& sceneObject : tileMapRoundTrip.scene().objects()) {
            auto* component = sceneObject->getComponent<scene2d::TileMapResourceComponent>();
            if (!component)
                continue;
            foundRoundTripTileMap = component->runtime() && component->map().ready()
                && component->desc().maxResidentChunks == 2u
                && component->desc().maxVisibleCells == 12u
                && component->desc().firstObjectId == 100u;
        }
        assert(foundRoundTripTileMap);

        assert(shell
                   .submitCommand(
                       editor::RemoveSceneComponentCommand{selected.value(), tileMap->id().value()})
                   .status
            == editor::EditorResponseStatus::Completed);
        shell.sceneDocument().setResourceManagerProvider(previousResourceProvider);
        shell.sceneDocument().setPhysicsWorld2DProvider([&editorPhysicsWorld] {
            return editorPhysicsWorld;
        });
        editor::SetSceneObjectTransformCommand restoreTileMapTransform;
        restoreTileMapTransform.objectId = selected.value();
        restoreTileMapTransform.translation = priorTransform.translation;
        restoreTileMapTransform.rotation = priorTransform.rotation;
        restoreTileMapTransform.scale = priorTransform.scale;
        assert(shell.submitCommand(restoreTileMapTransform).status
            == editor::EditorResponseStatus::Completed);
    }

    const auto componentCountBeforeConfiguredAdd =
        shell.sceneDocument().scene().findObject(selected)->components().size();
    editor::AddSceneComponentCommand configuredMeshAdd("MeshComponent");
    configuredMeshAdd.objectId = selected.value();
    editor::SetSceneComponentPropertyCommand initialColor("baseColor");
    initialColor.valueKind = editor::ScenePropertyValueKind::Color4;
    initialColor.color4Value = {0.2f, 0.4f, 0.6f, 0.8f};
    configuredMeshAdd.initialProperties.push_back(std::move(initialColor));
    assert(
        shell.submitCommand(configuredMeshAdd).status == editor::EditorResponseStatus::Completed);
    const auto* configuredMeshObject = shell.sceneDocument().scene().findObject(selected);
    assert(configuredMeshObject
        && configuredMeshObject->components().size() == componentCountBeforeConfiguredAdd + 1);
    const auto* configuredMesh = configuredMeshObject->getComponent<scene3d::MeshComponent>();
    assert(configuredMesh && configuredMesh->desc().baseColorTint.x == 0.2f
        && configuredMesh->desc().baseColorTint.y == 0.4f
        && configuredMesh->desc().baseColorTint.z == 0.6f
        && configuredMesh->desc().baseColorTint.w == 0.8f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(!shell.sceneDocument()
            .scene()
            .findObject(selected)
            ->getComponent<scene3d::MeshComponent>());
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    configuredMesh =
        shell.sceneDocument().scene().findObject(selected)->getComponent<scene3d::MeshComponent>();
    assert(configuredMesh && configuredMesh->desc().baseColorTint.x == 0.2f
        && configuredMesh->desc().baseColorTint.w == 0.8f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    const auto failedConfiguredAddVersion = shell.stateVersion();
    const auto failedConfiguredAddCount =
        shell.sceneDocument().scene().findObject(selected)->components().size();
    editor::AddSceneComponentCommand rejectedMeshAdd("MeshComponent");
    rejectedMeshAdd.objectId = selected.value();
    editor::SetSceneComponentPropertyCommand invalidInitialColor("baseColor");
    invalidInitialColor.valueKind = editor::ScenePropertyValueKind::Number;
    invalidInitialColor.numberValue = 0.5f;
    rejectedMeshAdd.initialProperties.push_back(std::move(invalidInitialColor));
    assert(shell.submitCommand(rejectedMeshAdd).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == failedConfiguredAddVersion
        && shell.sceneDocument().scene().findObject(selected)->components().size()
            == failedConfiguredAddCount);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("RigidBody2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* authoredRigidBody = shell.sceneDocument()
                                  .scene()
                                  .findObject(selected)
                                  ->getComponent<physics2d::RigidBody2DComponent>();
    assert(authoredRigidBody && authoredRigidBody->handle());
    const auto rigidBodyId = authoredRigidBody->id().value();
    const auto rigidBodyProperties = shell.sceneDocument().inspectorProperties(selected);
    assert(std::any_of(rigidBodyProperties.begin(), rigidBodyProperties.end(), [&](const auto& p) {
        return p.componentId == rigidBodyId && p.name == "type"
            && p.kind == editor::SceneInspectorPropertyKind::Enumeration && p.editable;
    }));
    editor::SetSceneComponentPropertyCommand setRigidBodyDamping("linearDamping");
    setRigidBodyDamping.objectId = selected.value();
    setRigidBodyDamping.componentId = rigidBodyId;
    setRigidBodyDamping.valueKind = editor::ScenePropertyValueKind::Number;
    setRigidBodyDamping.numberValue = 0.75f;
    assert(
        shell.submitCommand(setRigidBodyDamping).status == editor::EditorResponseStatus::Completed);
    assert(authoredRigidBody->desc().linearDamping == 0.75f);
    const auto validRigidBodyVersion = shell.stateVersion();
    auto invalidRigidBodyDamping = setRigidBodyDamping;
    invalidRigidBodyDamping.numberValue = -1.0f;
    assert(shell.submitCommand(invalidRigidBodyDamping).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == validRigidBodyVersion
        && authoredRigidBody->desc().linearDamping == 0.75f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredRigidBody->desc().linearDamping == 0.0f);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredRigidBody->desc().linearDamping == 0.75f);
    const auto duplicateRigidBodyVersion = shell.stateVersion();
    assert(shell.submitCommand(editor::AddSceneComponentCommand("RigidBody2DComponent")).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == duplicateRigidBodyVersion);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("RigidBody2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("Collider2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* authoredCollider = shell.sceneDocument()
                                 .scene()
                                 .findObject(selected)
                                 ->getComponent<physics2d::Collider2DComponent>();
    assert(authoredCollider && authoredCollider->handle());
    const auto colliderId = authoredCollider->id().value();
    const auto colliderInspector = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* field : {"type",
             "center",
             "halfSize",
             "pointA",
             "pointB",
             "radius",
             "points",
             "density",
             "friction",
             "restitution",
             "sensor",
             "loop"}) {
        assert(std::any_of(colliderInspector.begin(), colliderInspector.end(), [&](const auto& p) {
            return p.componentId == colliderId && p.name == field && p.editable
                && p.kind != editor::SceneInspectorPropertyKind::Unsupported;
        }));
    }
    const auto originalColliderHandle = authoredCollider->handle();
    editor::SetSceneComponentPropertyCommand setCollider2DRadius("radius");
    setCollider2DRadius.objectId = selected.value();
    setCollider2DRadius.componentId = colliderId;
    setCollider2DRadius.valueKind = editor::ScenePropertyValueKind::Number;
    setCollider2DRadius.numberValue = 0.75f;
    assert(
        shell.submitCommand(setCollider2DRadius).status == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().radius == 0.75f && authoredCollider->handle()
        && !(authoredCollider->handle() == originalColliderHandle));
    const auto colliderChangedVersion = shell.stateVersion();
    auto invalidColliderRadius = setCollider2DRadius;
    invalidColliderRadius.numberValue = 0.0f;
    assert(
        shell.submitCommand(invalidColliderRadius).status == editor::EditorResponseStatus::Failed);
    assert(
        shell.stateVersion() == colliderChangedVersion && authoredCollider->desc().radius == 0.75f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().radius == 0.5f && authoredCollider->handle());
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().radius == 0.75f && authoredCollider->handle());
    editor::SetSceneComponentPropertyCommand setCollider2DLoop("loop");
    setCollider2DLoop.objectId = selected.value();
    setCollider2DLoop.componentId = colliderId;
    setCollider2DLoop.valueKind = editor::ScenePropertyValueKind::Boolean;
    setCollider2DLoop.booleanValue = true;
    assert(
        shell.submitCommand(setCollider2DLoop).status == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().loop);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(!authoredCollider->desc().loop);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().loop);
    editor::SetSceneComponentPropertyCommand setCollider2DPoints("points");
    setCollider2DPoints.objectId = selected.value();
    setCollider2DPoints.componentId = colliderId;
    setCollider2DPoints.valueKind = editor::ScenePropertyValueKind::Vector2List;
    setCollider2DPoints.vector2ListValue = {{-1.0f, -0.5f}, {1.0f, -0.5f}, {0.0f, 1.0f}};
    assert(
        shell.submitCommand(setCollider2DPoints).status == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().points.size() == 3u
        && authoredCollider->desc().points[2].y == 1.0f);
    editor::SetSceneComponentPropertyCommand setCollider2DType("type");
    setCollider2DType.objectId = selected.value();
    setCollider2DType.componentId = colliderId;
    setCollider2DType.valueKind = editor::ScenePropertyValueKind::Enumeration;
    setCollider2DType.integerValue = static_cast<int>(physics2d::ShapeType::Convex);
    assert(
        shell.submitCommand(setCollider2DType).status == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().type == physics2d::ShapeType::Convex
        && authoredCollider->handle());
    const auto colliderPointsVersion = shell.stateVersion();
    auto invalidColliderPoints = setCollider2DPoints;
    invalidColliderPoints.vector2ListValue[1].x = std::numeric_limits<float>::quiet_NaN();
    assert(
        shell.submitCommand(invalidColliderPoints).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == colliderPointsVersion
        && authoredCollider->desc().points[1].x == 1.0f);
    auto oversizedColliderPoints = setCollider2DPoints;
    oversizedColliderPoints.vector2ListValue.resize(
        editor::kMaxSceneInspectorVector2ListElements + 1u);
    assert(shell.submitCommand(oversizedColliderPoints).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == colliderPointsVersion
        && authoredCollider->desc().points.size() == 3u);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().type == physics2d::ShapeType::Circle
        && authoredCollider->desc().points.size() == 3u);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().points.empty());
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().points.size() == 3u
        && authoredCollider->desc().points[0].x == -1.0f);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(authoredCollider->desc().type == physics2d::ShapeType::Convex);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    editor::SetSceneObjectTransformCommand restoreRigidBodyTestTransform;
    restoreRigidBodyTestTransform.translation = rigidBodyTestTransform.translation;
    restoreRigidBodyTestTransform.rotation = rigidBodyTestTransform.rotation;
    restoreRigidBodyTestTransform.scale = rigidBodyTestTransform.scale;
    assert(shell.submitCommand(restoreRigidBodyTestTransform).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("AnimatorComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* animatorComponent = shell.sceneDocument()
                                  .scene()
                                  .findObject(selected)
                                  ->getComponent<scene3d::AnimatorComponent>();
    assert(animatorComponent != nullptr);
    const auto animatorProperties = shell.sceneDocument().inspectorProperties(selected);
    const auto animatorId = animatorComponent->id().value();
    assert(std::any_of(animatorProperties.begin(), animatorProperties.end(), [&](const auto& p) {
        return p.componentId == animatorId && p.name == "prefabResourceId"
            && p.kind == editor::SceneInspectorPropertyKind::ResourceReference && p.editable
            && p.resourceType == "PrefabResource";
    }));
    assert(std::any_of(animatorProperties.begin(), animatorProperties.end(), [&](const auto& p) {
        return p.componentId == animatorId && p.name == "defaultStateName"
            && p.kind == editor::SceneInspectorPropertyKind::String && p.editable;
    }));
    editor::SetSceneComponentPropertyCommand setAnimatorDefaultState("defaultStateName");
    setAnimatorDefaultState.objectId = selected.value();
    setAnimatorDefaultState.componentId = animatorId;
    setAnimatorDefaultState.valueKind = editor::ScenePropertyValueKind::String;
    setAnimatorDefaultState.stringValue = "run";
    assert(shell.submitCommand(setAnimatorDefaultState).status
        == editor::EditorResponseStatus::Completed);
    assert(animatorComponent->editorAuthoringProperties()
               .cast<const scene3d::AnimatorEditorProperties&>()
               .defaultStateName
        == "run");
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(animatorComponent->editorAuthoringProperties()
            .cast<const scene3d::AnimatorEditorProperties&>()
            .defaultStateName.empty());
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(animatorComponent->editorAuthoringProperties()
               .cast<const scene3d::AnimatorEditorProperties&>()
               .defaultStateName
        == "run");
    auto invalidAnimatorProperties = animatorComponent->editorAuthoringProperties()
                                         .cast<const scene3d::AnimatorEditorProperties&>();
    invalidAnimatorProperties.prefabResourceId = "not-a-ready-prefab";
    assert(!animatorComponent->applyEditorAuthoringProperties(
        reflection::object(invalidAnimatorProperties)));
    const auto preservedAnimatorProperties = animatorComponent->editorAuthoringProperties()
                                                 .cast<const scene3d::AnimatorEditorProperties&>();
    assert(preservedAnimatorProperties.prefabResourceId.empty()
        && preservedAnimatorProperties.defaultStateName == "run"
        && !animatorComponent->editorBindingDiagnostic().empty());
    editor::SetSceneComponentPropertyCommand setAnimatorPrefab("prefabResourceId");
    setAnimatorPrefab.objectId = selected.value();
    setAnimatorPrefab.componentId = animatorId;
    setAnimatorPrefab.valueKind = editor::ScenePropertyValueKind::ResourceReference;
    setAnimatorPrefab.stringValue = "not-a-ready-prefab";
    const auto animatorStateVersion = shell.stateVersion();
    assert(shell.submitCommand(setAnimatorPrefab).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == animatorStateVersion
        && animatorComponent->editorAuthoringProperties()
            .cast<const scene3d::AnimatorEditorProperties&>()
            .prefabResourceId.empty());
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(tests::loadSpriteFixture(shell.projectResources()));
    constexpr const char* editorSpriteFontId = "00000000-0000-4000-8000-000000000005";
    scene2d::SpriteFontResourceDesc editorFontDesc;
    editorFontDesc.maxTextBytes = 64u;
    editorFontDesc.glyphs.push_back(
        {65u, core::pmr::String(tests::spriteA, &core::defaultCpuMemoryResource()), 0.5f});
    editorFontDesc.glyphs.push_back(
        {66u, core::pmr::String(tests::spriteB, &core::defaultCpuMemoryResource()), 0.75f});
    const auto editorFontLoad =
        shell.projectResources().loadResource(core::makeUnique<scene2d::SpriteFontResource>(
            core::pmr::String(editorSpriteFontId, &core::defaultCpuMemoryResource()),
            std::move(editorFontDesc)));
    assert(editorFontLoad.success);
    const auto readyEditorFont =
        shell.projectResources().find<scene2d::SpriteFontResource>(editorSpriteFontId);
    assert(readyEditorFont.ready() && readyEditorFont.get()->resolvedGlyphs().size() == 2u
        && readyEditorFont.get()->resolvedGlyphs()[1].advance == 0.75f
        && readyEditorFont.get()->resolvedGlyphs()[1].sprite.id() == tests::spriteB);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("SpriteTextComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* spriteText = shell.sceneDocument()
                           .scene()
                           .findObject(selected)
                           ->getComponent<scene2d::SpriteTextComponent>();
    assert(spriteText && spriteText->layout().glyphCount() == 0u);
    const auto spriteTextComponentId = spriteText->id().value();
    auto spriteTextProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* propertyName : {"fontResourceId", "text", "tint", "layer"})
        assert(std::any_of(
            spriteTextProperties.begin(), spriteTextProperties.end(), [&](const auto& property) {
                return property.componentId == spriteTextComponentId
                    && property.name == propertyName && property.editable;
            }));

    editor::SetSceneComponentPropertyCommand setSpriteTextFont("fontResourceId");
    setSpriteTextFont.objectId = selected.value();
    setSpriteTextFont.componentId = spriteTextComponentId;
    setSpriteTextFont.valueKind = editor::ScenePropertyValueKind::ResourceReference;
    setSpriteTextFont.stringValue = editorSpriteFontId;
    assert(
        shell.submitCommand(setSpriteTextFont).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setSpriteTextValue("text");
    setSpriteTextValue.objectId = selected.value();
    setSpriteTextValue.componentId = spriteTextComponentId;
    setSpriteTextValue.valueKind = editor::ScenePropertyValueKind::String;
    setSpriteTextValue.stringValue = "AB";
    assert(
        shell.submitCommand(setSpriteTextValue).status == editor::EditorResponseStatus::Completed);
    assert(spriteText->layout().glyphCount() == 2u);
    editor::SetSceneComponentPropertyCommand setSpriteTextTint("tint");
    setSpriteTextTint.objectId = selected.value();
    setSpriteTextTint.componentId = spriteTextComponentId;
    setSpriteTextTint.valueKind = editor::ScenePropertyValueKind::Color4;
    setSpriteTextTint.color4Value = {0.2f, 0.4f, 0.6f, 0.8f};
    assert(
        shell.submitCommand(setSpriteTextTint).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setSpriteTextLayer("layer");
    setSpriteTextLayer.objectId = selected.value();
    setSpriteTextLayer.componentId = spriteTextComponentId;
    setSpriteTextLayer.valueKind = editor::ScenePropertyValueKind::Integer;
    setSpriteTextLayer.integerValue = 37;
    assert(
        shell.submitCommand(setSpriteTextLayer).status == editor::EditorResponseStatus::Completed);
    const auto spriteTextAuthoring = spriteText->editorAuthoringProperties();
    const auto& spriteTextAuthoringValues =
        spriteTextAuthoring.cast<const scene2d::SpriteTextEditorProperties&>();
    assert(spriteTextAuthoringValues.fontResourceId == editorSpriteFontId
        && spriteTextAuthoringValues.text == "AB" && spriteText->layout().glyphCount() == 2u
        && spriteText->layer() == 37 && spriteText->color().w == 0.8f);
    const auto spriteTextVersionBeforeInvalidWrite = shell.stateVersion();
    setSpriteTextValue.stringValue = "C";
    assert(shell.submitCommand(setSpriteTextValue).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == spriteTextVersionBeforeInvalidWrite
        && spriteText->editorAuthoringProperties()
                .cast<const scene2d::SpriteTextEditorProperties&>()
                .text
            == "AB");
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(spriteText->layer() == 100);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(spriteText->layer() == 37);

    {
        auto firstResources = core::makeUnique<resource::ResourceManager>();
        resource::ResourceManager secondResources;
        assert(tests::loadSpriteFixture(*firstResources));
        assert(tests::loadSpriteFixture(secondResources));
        constexpr const char* switchedFontId = "00000000-0000-4000-8000-000000000006";
        const auto loadTestFont = [&](resource::ResourceManager& manager) {
            scene2d::SpriteFontResourceDesc desc;
            desc.maxTextBytes = 64u;
            desc.glyphs.push_back(
                {65u, core::pmr::String(tests::spriteA, &core::defaultCpuMemoryResource()), 0.5f});
            return manager.loadResource(core::makeUnique<scene2d::SpriteFontResource>(
                core::pmr::String(switchedFontId, &core::defaultCpuMemoryResource()),
                std::move(desc)));
        };
        assert(loadTestFont(*firstResources).success);
        assert(loadTestFont(secondResources).success);
        auto* activeResources = firstResources.get();
        core::SharedCallback<resource::ResourceManager*()> provider([&] {
            return activeResources;
        });
        scene2d::SpriteTextComponent switchedText(provider);
        scene2d::SpriteTextEditorProperties properties;
        properties.fontResourceId = switchedFontId;
        properties.text = "A";
        assert(switchedText.applyEditorAuthoringProperties(reflection::object(properties)));
        assert(switchedText.layout().glyphCount() == 1u);

        activeResources = &secondResources;
        firstResources.reset();
        switchedText.onUpdate({});
        assert(switchedText.layout().glyphCount() == 1u);

        activeResources = nullptr;
        switchedText.onUpdate({});
        assert(switchedText.layout().glyphCount() == 0u);
    }

    assert(
        shell.submitCommand(editor::AddSceneComponentCommand("ParticleEmitter2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* particleEmitter = shell.sceneDocument()
                                .scene()
                                .findObject(selected)
                                ->getComponent<scene2d::ParticleEmitter2DComponent>();
    assert(particleEmitter && !particleEmitter->spriteReady());
    const auto particleComponentId = particleEmitter->id().value();
    auto particleProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* propertyName : {"spriteId",
             "lifetimeSeconds",
             "size",
             "velocitySpread",
             "color",
             "seed",
             "layer",
             "order"}) {
        const bool propertyFound = std::any_of(
            particleProperties.begin(), particleProperties.end(), [&](const auto& property) {
                return property.componentId == particleComponentId && property.name == propertyName
                    && property.editable;
            });
        assert(propertyFound);
    }
    const auto particleVelocityProperty = std::find_if(
        particleProperties.begin(), particleProperties.end(), [&](const auto& property) {
            return property.componentId == particleComponentId && property.name == "velocitySpread";
        });
    assert(particleVelocityProperty != particleProperties.end()
        && particleVelocityProperty->kind == editor::SceneInspectorPropertyKind::Vector2);
    editor::SetSceneComponentPropertyCommand setParticleSprite("spriteId");
    setParticleSprite.objectId = selected.value();
    setParticleSprite.componentId = particleComponentId;
    setParticleSprite.valueKind = editor::ScenePropertyValueKind::ResourceReference;
    setParticleSprite.stringValue = tests::spriteA;
    assert(
        shell.submitCommand(setParticleSprite).status == editor::EditorResponseStatus::Completed);
    assert(particleEmitter->spriteReady() && particleEmitter->sprite().id() == tests::spriteA);
    editor::SetSceneComponentPropertyCommand setParticleLifetime("lifetimeSeconds");
    setParticleLifetime.objectId = selected.value();
    setParticleLifetime.componentId = particleComponentId;
    setParticleLifetime.valueKind = editor::ScenePropertyValueKind::Number;
    setParticleLifetime.numberValue = 2.5f;
    assert(
        shell.submitCommand(setParticleLifetime).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setParticleVelocity("velocitySpread");
    setParticleVelocity.objectId = selected.value();
    setParticleVelocity.componentId = particleComponentId;
    setParticleVelocity.valueKind = editor::ScenePropertyValueKind::Vector2;
    setParticleVelocity.vector2Value = {2.0f, 0.75f};
    assert(
        shell.submitCommand(setParticleVelocity).status == editor::EditorResponseStatus::Completed);
    assert(particleEmitter->editorAuthoringProperties()
               .cast<const scene2d::ParticleEmitter2DEditorProperties&>()
               .velocitySpread.x
        == 2.0f);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(particleEmitter->editorAuthoringProperties()
               .cast<const scene2d::ParticleEmitter2DEditorProperties&>()
               .velocitySpread.x
        == 0.0f);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(particleEmitter->editorAuthoringProperties()
               .cast<const scene2d::ParticleEmitter2DEditorProperties&>()
               .velocitySpread.y
        == 0.75f);
    const auto particleVersionBeforeInvalidSpread = shell.stateVersion();
    setParticleVelocity.vector2Value.y = -0.1f;
    assert(shell.submitCommand(setParticleVelocity).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == particleVersionBeforeInvalidSpread);
    setParticleVelocity.vector2Value.y = 0.75f;
    editor::SetSceneComponentPropertyCommand setParticleColor("color");
    setParticleColor.objectId = selected.value();
    setParticleColor.componentId = particleComponentId;
    setParticleColor.valueKind = editor::ScenePropertyValueKind::Color4;
    setParticleColor.color4Value = {0.4f, 0.5f, 0.6f, 0.7f};
    assert(shell.submitCommand(setParticleColor).status == editor::EditorResponseStatus::Completed);
    const auto particleVersionBeforeInvalidWrite = shell.stateVersion();
    setParticleLifetime.numberValue = std::numeric_limits<float>::quiet_NaN();
    assert(shell.submitCommand(setParticleLifetime).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == particleVersionBeforeInvalidWrite);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(particleEmitter->spriteReady());
    assert(particleEmitter->editorAuthoringProperties()
               .cast<const scene2d::ParticleEmitter2DEditorProperties&>()
               .color.w
        == 1.0f);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(particleEmitter->spriteReady() && particleEmitter->sprite().id() == tests::spriteA);
    assert(particleEmitter->editorAuthoringProperties()
               .cast<const scene2d::ParticleEmitter2DEditorProperties&>()
               .color.w
        == 0.7f);
    scene::SceneUpdateContext particleStep;
    particleStep.deltaSeconds = 1.0f / 60.0f;
    particleEmitter->onFixedUpdate(particleStep);
    assert(particleEmitter->burst({}, 3u));
    particleEmitter->onFixedUpdate(particleStep);
    assert(!particleEmitter->particles().empty());

    {
        auto firstResources = core::makeUnique<resource::ResourceManager>();
        resource::ResourceManager secondResources;
        assert(tests::loadSpriteFixture(*firstResources));
        assert(tests::loadSpriteFixture(secondResources));
        auto* activeResources = firstResources.get();
        core::SharedCallback<resource::ResourceManager*()> provider([&] {
            return activeResources;
        });
        scene2d::ParticleEmitter2DComponent emitter(provider, 128u);
        scene2d::ParticleEmitter2DEditorProperties properties;
        properties.spriteId = tests::spriteA;
        assert(emitter.applyEditorAuthoringProperties(reflection::object(properties)));
        assert(emitter.spriteReady() && emitter.sprite().id() == tests::spriteA);

        activeResources = &secondResources;
        firstResources.reset();
        assert(!emitter.spriteReady());
        emitter.onFixedUpdate(particleStep);
        assert(emitter.spriteReady() && emitter.sprite().id() == tests::spriteA);

        activeResources = nullptr;
        assert(!emitter.spriteReady());
    }

    core::pmr::Vector<scene2d::SpriteAnimationFrame> editorAnimationFrames;
    editorAnimationFrames.push_back({core::pmr::String(tests::spriteA), 0.125, 11});
    editorAnimationFrames.push_back({core::pmr::String(tests::spriteB), 0.125, 12});
    constexpr const char* editorAnimationId = "00000000-0000-4000-8000-000000000099";
    assert(shell.projectResources()
            .load<scene2d::SpriteAnimationResource>(
                {}, core::pmr::String(editorAnimationId), editorAnimationFrames, true)
            .success);

    {
        auto firstResources = core::makeUnique<resource::ResourceManager>();
        resource::ResourceManager secondResources;
        assert(tests::loadSpriteFixture(*firstResources));
        assert(tests::loadSpriteFixture(secondResources));
        constexpr const char* switchedAnimationId = "00000000-0000-4000-8000-000000000098";
        core::pmr::Vector<scene2d::SpriteAnimationFrame> firstFrames;
        firstFrames.push_back({core::pmr::String(tests::spriteA), 0.125, 21});
        core::pmr::Vector<scene2d::SpriteAnimationFrame> secondFrames;
        secondFrames.push_back({core::pmr::String(tests::spriteB), 0.125, 22});
        assert(firstResources
                ->load<scene2d::SpriteAnimationResource>(
                    {}, core::pmr::String(switchedAnimationId), firstFrames, true)
                .success);
        assert(secondResources
                .load<scene2d::SpriteAnimationResource>(
                    {}, core::pmr::String(switchedAnimationId), secondFrames, true)
                .success);

        auto* activeResources = firstResources.get();
        core::SharedCallback<resource::ResourceManager*()> provider([&] {
            return activeResources;
        });
        scene2d::SpriteAnimationComponent animation(provider, 8u);
        auto properties = scene2d::SpriteAnimationEditorProperties{};
        properties.animationResourceId = switchedAnimationId;
        assert(animation.applyEditorAuthoringProperties(reflection::object(properties)));
        assert(animation.valid() && animation.animationReady());
        assert(
            animation.animator().spriteId() && *animation.animator().spriteId() == tests::spriteA);

        activeResources = &secondResources;
        firstResources.reset();
        assert(!animation.valid() && !animation.animationReady());
        assert(animation.advance(0.0));
        assert(animation.valid() && animation.animationReady());
        assert(
            animation.animator().spriteId() && *animation.animator().spriteId() == tests::spriteB);
        assert(animation.animationResourceId() == switchedAnimationId);

        activeResources = nullptr;
        assert(!animation.valid() && !animation.animationReady());
        assert(!animation.advance(0.0));
    }

    assert(shell.submitCommand(editor::AddSceneComponentCommand("SpriteRendererComponent")).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("SpriteAnimationComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* spriteAnimation = shell.sceneDocument()
                                .scene()
                                .findObject(selected)
                                ->getComponent<scene2d::SpriteAnimationComponent>();
    assert(spriteAnimation && !spriteAnimation->valid());
    const auto spriteAnimationId = spriteAnimation->id().value();
    auto animationProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* propertyName : {"animationResourceId", "speed", "paused", "eventBudget"})
        assert(std::any_of(
            animationProperties.begin(), animationProperties.end(), [&](const auto& property) {
                return property.componentId == spriteAnimationId && property.name == propertyName
                    && property.editable;
            }));

    editor::SetSceneComponentPropertyCommand setAnimationResource("animationResourceId");
    setAnimationResource.objectId = selected.value();
    setAnimationResource.componentId = spriteAnimationId;
    setAnimationResource.valueKind = editor::ScenePropertyValueKind::ResourceReference;
    setAnimationResource.stringValue = editorAnimationId;
    assert(shell.submitCommand(setAnimationResource).status
        == editor::EditorResponseStatus::Completed);
    assert(spriteAnimation->valid() && spriteAnimation->animationReady()
        && spriteAnimation->animationResourceId() == editorAnimationId);

    editor::SetSceneComponentPropertyCommand setAnimationSpeed("speed");
    setAnimationSpeed.objectId = selected.value();
    setAnimationSpeed.componentId = spriteAnimationId;
    setAnimationSpeed.valueKind = editor::ScenePropertyValueKind::Number;
    setAnimationSpeed.numberValue = 1.75f;
    assert(
        shell.submitCommand(setAnimationSpeed).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setAnimationPaused("paused");
    setAnimationPaused.objectId = selected.value();
    setAnimationPaused.componentId = spriteAnimationId;
    setAnimationPaused.valueKind = editor::ScenePropertyValueKind::Boolean;
    setAnimationPaused.booleanValue = true;
    assert(
        shell.submitCommand(setAnimationPaused).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setAnimationBudget("eventBudget");
    setAnimationBudget.objectId = selected.value();
    setAnimationBudget.componentId = spriteAnimationId;
    setAnimationBudget.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
    setAnimationBudget.unsignedIntegerValue = 64u;
    assert(
        shell.submitCommand(setAnimationBudget).status == editor::EditorResponseStatus::Completed);
    assert(spriteAnimation->speed() == 1.75 && spriteAnimation->paused()
        && spriteAnimation->eventBudget() == 64u && spriteAnimation->valid());
    const auto animationVersionBeforeInvalidEdit = shell.stateVersion();
    setAnimationResource.stringValue = "missing-animation";
    assert(
        shell.submitCommand(setAnimationResource).status == editor::EditorResponseStatus::Failed);
    setAnimationBudget.unsignedIntegerValue = 0u;
    assert(shell.submitCommand(setAnimationBudget).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == animationVersionBeforeInvalidEdit);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(spriteAnimation->eventBudget() == scene2d::kEditorSpriteAnimationEventBudget);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(spriteAnimation->eventBudget() == 64u);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    const auto animationDependencyStateVersion = shell.stateVersion();
    assert(shell.submitCommand(editor::AddSceneComponentCommand("SpriteAnimationComponent")).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == animationDependencyStateVersion);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("TimerComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* timerComponent =
        shell.sceneDocument().scene().findObject(selected)->getComponent<scene::TimerComponent>();
    assert(timerComponent);
    auto timerProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* propertyName : {"seconds", "repeat", "startOnEnable", "eventName"})
        assert(
            std::any_of(timerProperties.begin(), timerProperties.end(), [&](const auto& property) {
                return property.componentId == timerComponent->id().value()
                    && property.name == propertyName && property.editable;
            }));
    editor::SetSceneComponentPropertyCommand setTimerDuration("seconds");
    setTimerDuration.objectId = selected.value();
    setTimerDuration.componentId = timerComponent->id().value();
    setTimerDuration.valueKind = editor::ScenePropertyValueKind::Number;
    setTimerDuration.numberValue = 2.5f;
    assert(shell.submitCommand(setTimerDuration).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setTimerEvent("eventName");
    setTimerEvent.objectId = selected.value();
    setTimerEvent.componentId = timerComponent->id().value();
    setTimerEvent.valueKind = editor::ScenePropertyValueKind::String;
    setTimerEvent.stringValue = "timer.elapsed";
    assert(shell.submitCommand(setTimerEvent).status == editor::EditorResponseStatus::Completed);
    const auto timerVersionBeforeInvalidWrite = shell.stateVersion();
    setTimerDuration.numberValue = 0.0f;
    assert(shell.submitCommand(setTimerDuration).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == timerVersionBeforeInvalidWrite);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("TweenComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* tweenComponent =
        shell.sceneDocument().scene().findObject(selected)->getComponent<scene::TweenComponent>();
    assert(tweenComponent);
    const auto tweenProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* propertyName : {"seconds", "from", "to", "unscaled", "startOnEnable"})
        assert(
            std::any_of(tweenProperties.begin(), tweenProperties.end(), [&](const auto& property) {
                return property.componentId == tweenComponent->id().value()
                    && property.name == propertyName && property.editable;
            }));
    editor::SetSceneComponentPropertyCommand setTweenTo("to");
    setTweenTo.objectId = selected.value();
    setTweenTo.componentId = tweenComponent->id().value();
    setTweenTo.valueKind = editor::ScenePropertyValueKind::Number;
    setTweenTo.numberValue = 8.0f;
    assert(shell.submitCommand(setTweenTo).status == editor::EditorResponseStatus::Completed);
    editor::SetSceneComponentPropertyCommand setTweenDuration("seconds");
    setTweenDuration.objectId = selected.value();
    setTweenDuration.componentId = tweenComponent->id().value();
    setTweenDuration.valueKind = editor::ScenePropertyValueKind::Number;
    setTweenDuration.numberValue = std::numeric_limits<float>::quiet_NaN();
    const auto tweenVersionBeforeInvalidWrite = shell.stateVersion();
    assert(shell.submitCommand(setTweenDuration).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == tweenVersionBeforeInvalidWrite);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("ScreenCanvas2DComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* screenCanvas = shell.sceneDocument()
                             .scene()
                             .findObject(selected)
                             ->getComponent<scene2d::ScreenCanvas2DComponent>();
    assert(screenCanvas && screenCanvas->pixelsPerUnit() == 100.0f);
    const auto canvasProperties = shell.sceneDocument().inspectorProperties(selected);
    assert(std::any_of(canvasProperties.begin(), canvasProperties.end(), [&](const auto& property) {
        return property.componentId == screenCanvas->id().value()
            && property.name == "pixelsPerUnit"
            && property.kind == editor::SceneInspectorPropertyKind::Number && property.editable;
    }));
    editor::SetSceneComponentPropertyCommand setPixelsPerUnit("pixelsPerUnit");
    setPixelsPerUnit.objectId = selected.value();
    setPixelsPerUnit.componentId = screenCanvas->id().value();
    setPixelsPerUnit.valueKind = editor::ScenePropertyValueKind::Number;
    setPixelsPerUnit.numberValue = 64.0f;
    assert(shell.submitCommand(setPixelsPerUnit).status == editor::EditorResponseStatus::Completed);
    assert(screenCanvas->pixelsPerUnit() == 64.0f);
    setPixelsPerUnit.numberValue = 0.0f;
    const auto invalidCanvasWrite = shell.submitCommand(setPixelsPerUnit);
    assert(invalidCanvasWrite.status == editor::EditorResponseStatus::Failed);
    assert(screenCanvas->pixelsPerUnit() == 64.0f);
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("AudioListenerComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* audioListener = shell.sceneDocument()
                              .scene()
                              .findObject(selected)
                              ->getComponent<audio::AudioListenerComponent>();
    assert(audioListener);
    const auto audioListenerProperties = shell.sceneDocument().inspectorProperties(selected);
    assert(std::any_of(
        audioListenerProperties.begin(), audioListenerProperties.end(), [&](const auto& property) {
            return property.componentId == audioListener->id().value() && property.name == "enabled"
                && property.editable
                && property.kind == editor::SceneInspectorPropertyKind::Boolean;
        }));
    editor::SetSceneComponentPropertyCommand disableAudioListener("enabled");
    disableAudioListener.objectId = selected.value();
    disableAudioListener.componentId = audioListener->id().value();
    disableAudioListener.valueKind = editor::ScenePropertyValueKind::Boolean;
    disableAudioListener.booleanValue = false;
    assert(shell.submitCommand(disableAudioListener).status
        == editor::EditorResponseStatus::Completed);
    assert(!audioListener->enabled());
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("TerrainComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* terrainComponent = shell.sceneDocument()
                                 .scene()
                                 .findObject(selected)
                                 ->getComponent<scene3d::TerrainComponent>();
    assert(terrainComponent);
    auto terrainProperties = shell.sceneDocument().inspectorProperties(selected);
    const auto terrainResourceProperty =
        std::find_if(terrainProperties.begin(), terrainProperties.end(), [&](const auto& property) {
            return property.componentId == terrainComponent->id().value()
                && property.name == "resourceId"
                && property.kind == editor::SceneInspectorPropertyKind::ResourceReference
                && property.editable;
        });
    assert(terrainResourceProperty != terrainProperties.end());
    editor::SetSceneComponentPropertyCommand setTerrainResource("resourceId");
    setTerrainResource.objectId = selected.value();
    setTerrainResource.componentId = terrainComponent->id().value();
    setTerrainResource.valueKind = editor::ScenePropertyValueKind::ResourceReference;
    setTerrainResource.stringValue = "19b0e6f7-c941-4f6a-9e82-7a03ac10be55";
    assert(shell.submitCommand(setTerrainResource).status == editor::EditorResponseStatus::Failed);
    assert(terrainComponent->resourceId().empty());
    const auto terrainVersion = shell.stateVersion();
    setTerrainResource.stringValue = "terrain-path.glb";
    assert(shell.submitCommand(setTerrainResource).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == terrainVersion);
    assert(terrainComponent->resourceId().empty());
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    const std::array<std::pair<const char*, const char*>, 6> authoringComponents{{
        {"TerrainAuthoringComponent", "halfExtent"},
        {"WaterSurfaceComponent", "segments"},
        {"RoadNetworkComponent", "generationEnabled"},
        {"CritterFieldComponent", "worldMaxX"},
        {"SkyComponent", "sunDirection"},
        {"CampfireComponent", "position"},
    }};
    for (const auto& [type, propertyName] : authoringComponents) {
        assert(shell.submitCommand(editor::AddSceneComponentCommand(type)).status
            == editor::EditorResponseStatus::Completed);
        const auto* selectedObject = shell.sceneDocument().scene().findObject(selected);
        assert(selectedObject);
        const auto* component = selectedObject->components().back().get();
        assert(std::string_view(component->scriptTypeName()) == type);
        const auto properties = shell.sceneDocument().inspectorProperties(selected);
        assert(std::any_of(properties.begin(), properties.end(), [&](const auto& property) {
            return property.componentId == component->id().value() && property.name == propertyName
                && property.editable;
        }));
    }
    const auto* terrainAuthoringForValidation =
        shell.sceneDocument()
            .scene()
            .findObject(selected)
            ->getComponent<scene3d::TerrainAuthoringComponent>();
    assert(terrainAuthoringForValidation);
    const auto originalTerrainHalfExtent = terrainAuthoringForValidation->half_extent();
    editor::SetSceneComponentPropertyCommand invalidTerrainHalfExtent("halfExtent");
    invalidTerrainHalfExtent.objectId = selected.value();
    invalidTerrainHalfExtent.componentId = terrainAuthoringForValidation->id().value();
    invalidTerrainHalfExtent.valueKind = editor::ScenePropertyValueKind::Number;
    invalidTerrainHalfExtent.numberValue = -1.0f;
    const auto invalidTerrainHalfExtentVersion = shell.stateVersion();
    assert(shell.submitCommand(invalidTerrainHalfExtent).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == invalidTerrainHalfExtentVersion);
    assert(terrainAuthoringForValidation->half_extent() == originalTerrainHalfExtent);
    const auto* critterFieldForValidation = shell.sceneDocument()
                                                .scene()
                                                .findObject(selected)
                                                ->getComponent<scene3d::CritterFieldComponent>();
    assert(critterFieldForValidation);
    const auto originalCritterMaxX = critterFieldForValidation->world_max_x();
    editor::SetSceneComponentPropertyCommand invalidCritterMaxX("worldMaxX");
    invalidCritterMaxX.objectId = selected.value();
    invalidCritterMaxX.componentId = critterFieldForValidation->id().value();
    invalidCritterMaxX.valueKind = editor::ScenePropertyValueKind::Number;
    invalidCritterMaxX.numberValue = 0.0f;
    const auto invalidCritterMaxXVersion = shell.stateVersion();
    assert(shell.submitCommand(invalidCritterMaxX).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == invalidCritterMaxXVersion);
    assert(critterFieldForValidation->world_max_x() == originalCritterMaxX);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("ColliderComponent")).status
        == editor::EditorResponseStatus::Completed);
    const auto* colliderForCatalog = shell.sceneDocument()
                                         .scene()
                                         .findObject(selected)
                                         ->getComponent<scene3d::ColliderComponent>();
    assert(colliderForCatalog && !colliderForCatalog->hasBody());
    const auto colliderCatalogProperties = shell.sceneDocument().inspectorProperties(selected);
    assert(std::any_of(colliderCatalogProperties.begin(),
        colliderCatalogProperties.end(),
        [&](const auto& property) {
            return property.componentId == colliderForCatalog->id().value()
                && property.name == "body.shape.radius" && property.editable;
        }));
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    assert(shell.submitCommand(editor::AddSceneComponentCommand("ResourceRequestComponent")).status
        == editor::EditorResponseStatus::Completed);
    const auto* selectedResourceObject = shell.sceneDocument().scene().findObject(selected);
    const auto* resourceRequest =
        selectedResourceObject->getComponent<resource::ResourceRequestComponent>();
    assert(resourceRequest);
    const auto resourceProperties = shell.sceneDocument().inspectorProperties(selected);
    for (const auto* field : {"meshAsset", "terrainRecipe", "pcgProfile"})
        assert(std::any_of(
            resourceProperties.begin(), resourceProperties.end(), [&](const auto& property) {
                return property.componentId == resourceRequest->id().value()
                    && property.name == field && property.editable
                    && property.kind == editor::SceneInspectorPropertyKind::String;
            }));

    editor::SetSceneComponentPropertyCommand setMeshAsset("meshAsset");
    setMeshAsset.objectId = selected.value();
    setMeshAsset.componentId = resourceRequest->id().value();
    setMeshAsset.valueKind = editor::ScenePropertyValueKind::String;
    setMeshAsset.stringValue = "assets/meshes/house.glb";
    assert(shell.submitCommand(setMeshAsset).status == editor::EditorResponseStatus::Completed);
    assert(resourceRequest->mesh_asset() == "assets/meshes/house.glb");
    setMeshAsset.stringValue.clear();
    assert(shell.submitCommand(setMeshAsset).status == editor::EditorResponseStatus::Completed);
    assert(resourceRequest->mesh_asset().empty());

    setMeshAsset.stringValue.assign(513u, 'a');
    const auto pathStateVersion = shell.stateVersion();
    assert(shell.submitCommand(setMeshAsset).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == pathStateVersion);
    assert(resourceRequest->mesh_asset().empty());
    for (const auto* invalidPath : {"/outside/project/mesh.glb",
             "C:/project/mesh.glb",
             "assets\\mesh.glb",
             "assets/../outside/mesh.glb",
             "assets/"}) {
        setMeshAsset.stringValue = invalidPath;
        assert(shell.submitCommand(setMeshAsset).status == editor::EditorResponseStatus::Failed);
        assert(shell.stateVersion() == pathStateVersion);
        assert(resourceRequest->mesh_asset().empty());
    }
    assert(shell.submitCommand(editor::RemoveSceneComponentCommand{}).status
        == editor::EditorResponseStatus::Completed);

    const auto* selectedObject = shell.sceneDocument().scene().findObject(selected);
    const auto* water = selectedObject->getComponent<scene3d::WaterSurfaceComponent>();
    const auto* road = selectedObject->getComponent<scene3d::RoadNetworkComponent>();
    const auto* sky = selectedObject->getComponent<scene3d::SkyComponent>();
    assert(water && road && sky);
    editor::SetSceneComponentPropertyCommand setWaterSegments("segments");
    setWaterSegments.objectId = selected.value();
    setWaterSegments.componentId = water->id().value();
    setWaterSegments.valueKind = editor::ScenePropertyValueKind::Integer;
    setWaterSegments.integerValue = 192;
    assert(shell.submitCommand(setWaterSegments).status == editor::EditorResponseStatus::Completed);
    assert(water->segments() == 192);
    editor::SetSceneComponentPropertyCommand setRoadEnabled("generationEnabled");
    setRoadEnabled.objectId = selected.value();
    setRoadEnabled.componentId = road->id().value();
    setRoadEnabled.valueKind = editor::ScenePropertyValueKind::Boolean;
    setRoadEnabled.booleanValue = true;
    assert(shell.submitCommand(setRoadEnabled).status == editor::EditorResponseStatus::Completed);
    assert(road->generation_enabled());
    editor::SetSceneComponentPropertyCommand setSkyDirection("sunDirection");
    setSkyDirection.objectId = selected.value();
    setSkyDirection.componentId = sky->id().value();
    setSkyDirection.valueKind = editor::ScenePropertyValueKind::Vector3;
    setSkyDirection.vector3Value = {1.0f, 2.0f, 3.0f};
    assert(shell.submitCommand(setSkyDirection).status == editor::EditorResponseStatus::Completed);
    assert(sky->sun_direction().x == 1.0f && sky->sun_direction().y == 2.0f
        && sky->sun_direction().z == 3.0f);

    const auto* lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    assert(lightObject);
    const auto* light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light);
    const auto lightProperties = shell.sceneDocument().inspectorProperties(lightObject->id());
    assert(std::any_of(lightProperties.begin(), lightProperties.end(), [](const auto& property) {
        return property.name == "intensity" && property.editable
            && property.kind == editor::SceneInspectorPropertyKind::Number;
    }));
    assert(std::any_of(lightProperties.begin(), lightProperties.end(), [](const auto& property) {
        return property.name == "castShadow" && property.editable
            && property.kind == editor::SceneInspectorPropertyKind::Boolean;
    }));
    assert(std::any_of(lightProperties.begin(), lightProperties.end(), [](const auto& property) {
        return property.name == "type" && property.editable
            && property.kind == editor::SceneInspectorPropertyKind::Enumeration
            && property.enumOptions.size() == 3;
    }));
    assert(std::any_of(lightProperties.begin(), lightProperties.end(), [](const auto& property) {
        return property.name == "color" && property.editable
            && property.kind == editor::SceneInspectorPropertyKind::Vector3;
    }));

    assert(shell.submitCommand(editor::SelectSceneObjectCommand{lightObject->id().value()}).status
        == editor::EditorResponseStatus::Completed);
    core::pmr::String typeSource("scene_property_", &core::defaultCpuMemoryResource());
    typeSource += std::to_string(lightObject->id().value());
    typeSource.push_back('_');
    typeSource += std::to_string(light->id().value());
    typeSource += "_type";
    ui::WidgetId typeWidget;
    core::pmr::String directionXSource("scene_property_", &core::defaultCpuMemoryResource());
    directionXSource += std::to_string(lightObject->id().value());
    directionXSource.push_back('_');
    directionXSource += std::to_string(light->id().value());
    directionXSource += "_direction_x";
    ui::WidgetId directionXWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (!widget)
            continue;
        if (widget->name() == typeSource)
            typeWidget = widgetId;
        if (widget->name() == directionXSource)
            directionXWidget = widgetId;
    }
    assert(typeWidget.valid() && directionXWidget.valid());
    const auto* typePropertyWidget = shell.runtime().canvas().findWidget(typeWidget);
    assert(typePropertyWidget && typePropertyWidget->parentId() == inspectorContentId);
    assert(typePropertyWidget->desc().localRect.x >= 0.0f
        && typePropertyWidget->desc().localRect.x + typePropertyWidget->desc().localRect.width
            <= 302.0f);
    const auto typeProperty =
        std::find_if(lightProperties.begin(), lightProperties.end(), [](const auto& property) {
            return property.name == "type";
        });
    assert(typeProperty != lightProperties.end());
    std::uint32_t pointOptionIndex = 0;
    for (std::size_t index = 0; index < typeProperty->enumOptions.size(); ++index)
        if (typeProperty->enumOptions[index].name == "Point")
            pointOptionIndex = static_cast<std::uint32_t>(index);
    const auto* typeControl = shell.runtime().findControlObject(typeWidget);
    assert(typeControl && typeControl->controlType() == ui::UiControlType::Dropdown);
    assert(shell.runtime().interact(ui::UiControlInteraction::click(typeWidget)).ok());
    core::pmr::String pointOptionSource(&core::defaultCpuMemoryResource());
    pointOptionSource.append(typeSource.data(), typeSource.size());
    pointOptionSource.append("_option_");
    std::array<char, 10> pointOptionIndexText{};
    const auto [pointOptionIndexEnd, pointOptionIndexError] =
        std::to_chars(pointOptionIndexText.data(),
            pointOptionIndexText.data() + pointOptionIndexText.size(),
            pointOptionIndex);
    assert(pointOptionIndexError == std::errc{});
    pointOptionSource.append(pointOptionIndexText.data(),
        static_cast<std::size_t>(pointOptionIndexEnd - pointOptionIndexText.data()));
    ui::WidgetId pointOptionWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == pointOptionSource)
            pointOptionWidget = widgetId;
    }
    assert(pointOptionWidget.valid());
    assert(shell.runtime().interact(ui::UiControlInteraction::click(pointOptionWidget)).ok());
    assert(light->desc().type == scene3d::LightType::Point);
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(
                directionXWidget, core::pmr::String("2.5", &core::defaultCpuMemoryResource())))
            .ok());
    assert(light->desc().direction.x == 2.5f);
    core::pmr::String intensitySource("scene_property_", &core::defaultCpuMemoryResource());
    intensitySource += std::to_string(lightObject->id().value());
    intensitySource.push_back('_');
    intensitySource += std::to_string(light->id().value());
    intensitySource += "_intensity";
    ui::WidgetId intensityWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == intensitySource)
            intensityWidget = widgetId;
    }
    assert(intensityWidget.valid());
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(
                intensityWidget, core::pmr::String("4.25", &core::defaultCpuMemoryResource())))
            .ok());
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().intensity == 4.25f);
    const auto versionBeforeInvalidPropertyInput = shell.stateVersion();
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::editText(
                intensityWidget, core::pmr::String("nan", &core::defaultCpuMemoryResource())))
            .ok());
    assert(shell.stateVersion() == versionBeforeInvalidPropertyInput);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().intensity == 4.25f);
    assert(shell.runtime().canvas().findWidget(intensityWidget) == nullptr);
    ui::WidgetId refreshedIntensityWidget;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == intensitySource)
            refreshedIntensityWidget = widgetId;
    }
    assert(refreshedIntensityWidget.valid() && refreshedIntensityWidget != intensityWidget);
    assert(shell.submitCommand(editor::SelectSceneObjectCommand{selected.value()}).status
        == editor::EditorResponseStatus::Completed);

    editor::SetSceneComponentPropertyCommand setLightIntensity("intensity");
    setLightIntensity.objectId = lightObject->id().value();
    setLightIntensity.componentId = light->id().value();
    setLightIntensity.valueKind = editor::ScenePropertyValueKind::Number;
    setLightIntensity.numberValue = 4.25f;
    assert(
        shell.submitCommand(setLightIntensity).status == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().intensity == 4.25f);
    auto setLightRange = setLightIntensity;
    setLightRange.name = "range";
    setLightRange.numberValue = 18.0f;
    assert(shell.submitCommand(setLightRange).status == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().range == 18.0f);

    auto setLightType = setLightIntensity;
    setLightType.name = "type";
    setLightType.valueKind = editor::ScenePropertyValueKind::Enumeration;
    setLightType.integerValue = static_cast<int>(scene3d::LightType::Spot);
    assert(shell.submitCommand(setLightType).status == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().type == scene3d::LightType::Spot);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().type == scene3d::LightType::Point);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().type == scene3d::LightType::Spot);
    const auto stateBeforeInvalidEnum = shell.stateVersion();
    auto invalidEnum = setLightType;
    invalidEnum.integerValue = 99;
    assert(shell.submitCommand(invalidEnum).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == stateBeforeInvalidEnum);
    assert(shell.submitCommand(setLightType).status == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().type == scene3d::LightType::Point);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);

    auto setLightColor = setLightIntensity;
    setLightColor.name = "color";
    setLightColor.valueKind = editor::ScenePropertyValueKind::Vector3;
    setLightColor.vector3Value = {0.25f, 0.5f, 0.75f};
    assert(shell.submitCommand(setLightColor).status == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().color.x == 0.25f && light->desc().color.y == 0.5f
        && light->desc().color.z == 0.75f);

    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().color.x == 1.0f && light->desc().color.y == 1.0f
        && light->desc().color.z == 1.0f);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().color.x == 0.25f && light->desc().color.y == 0.5f
        && light->desc().color.z == 0.75f);

    assert(shell.submitCommand(editor::UndoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    auto forkedColor = setLightColor;
    forkedColor.vector3Value = {0.4f, 0.5f, 0.6f};
    assert(shell.submitCommand(forkedColor).status == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::RedoSceneCommand{}).status
        == editor::EditorResponseStatus::Completed);
    lightObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
    light = lightObject->getComponent<scene3d::LightComponent>();
    assert(light->desc().color.x == 0.4f && light->desc().color.y == 0.5f
        && light->desc().color.z == 0.6f);

    const auto stateBeforeInvalidProperty = shell.stateVersion();
    auto wrongKind = setLightIntensity;
    wrongKind.name = "castShadow";
    assert(shell.submitCommand(wrongKind).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == stateBeforeInvalidProperty);
    auto unknownProperty = setLightIntensity;
    unknownProperty.name = "rendererHandle";
    assert(shell.submitCommand(unknownProperty).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == stateBeforeInvalidProperty);

    const auto* cameraObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("Camera3D", &core::defaultCpuMemoryResource()));
    assert(cameraObject);
    const auto* camera = cameraObject->getComponent<scene::CameraComponent>();
    assert(camera);
    editor::SetSceneComponentPropertyCommand setCameraPriority("priority");
    setCameraPriority.objectId = cameraObject->id().value();
    setCameraPriority.componentId = camera->id().value();
    setCameraPriority.valueKind = editor::ScenePropertyValueKind::Integer;
    setCameraPriority.integerValue = 3;
    assert(
        shell.submitCommand(setCameraPriority).status == editor::EditorResponseStatus::Completed);
    cameraObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("Camera3D", &core::defaultCpuMemoryResource()));
    camera = cameraObject->getComponent<scene::CameraComponent>();
    assert(camera->desc().priority == 3);
    editor::SetSceneComponentPropertyCommand setCameraFov("verticalFovDegrees");
    setCameraFov.objectId = cameraObject->id().value();
    setCameraFov.componentId = camera->id().value();
    setCameraFov.valueKind = editor::ScenePropertyValueKind::Number;
    setCameraFov.numberValue = 75.0f;
    assert(shell.submitCommand(setCameraFov).status == editor::EditorResponseStatus::Completed);
    cameraObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("Camera3D", &core::defaultCpuMemoryResource()));
    camera = cameraObject->getComponent<scene::CameraComponent>();
    assert(camera->desc().verticalFovDegrees == 75.0f);

    auto* colliderObject = shell.sceneDocument().scene().findObjectByName(
        core::pmr::String("Camera3D", &core::defaultCpuMemoryResource()));
    assert(colliderObject);
    assert(
        shell.submitCommand(editor::SelectSceneObjectCommand{colliderObject->id().value()}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::AddSceneComponentCommand("ColliderComponent")).status
        == editor::EditorResponseStatus::Completed);
    auto* collider = colliderObject->getComponent<scene3d::ColliderComponent>();
    assert(collider && !collider->hasBody());
    const auto colliderProperties = shell.sceneDocument().inspectorProperties(colliderObject->id());
    assert(
        std::any_of(colliderProperties.begin(), colliderProperties.end(), [](const auto& property) {
            return property.componentType == "ColliderComponent"
                && property.name == "body.shape.radius" && property.editable
                && property.kind == editor::SceneInspectorPropertyKind::Number;
        }));
    assert(
        std::any_of(colliderProperties.begin(), colliderProperties.end(), [](const auto& property) {
            return property.componentType == "ColliderComponent" && property.name == "body.layer"
                && property.editable
                && property.kind == editor::SceneInspectorPropertyKind::Enumeration
                && property.enumOptions.size() == 4
                && std::none_of(property.enumOptions.begin(),
                    property.enumOptions.end(),
                    [](const auto& option) {
                        return option.name == "Count";
                    });
        }));
    const auto shapeKind = std::find_if(
        colliderProperties.begin(), colliderProperties.end(), [](const auto& property) {
            return property.name == "body.shape.kind";
        });
    assert(shapeKind != colliderProperties.end() && shapeKind->editable
        && shapeKind->kind == editor::SceneInspectorPropertyKind::Enumeration
        && shapeKind->enumOptions.size() == 7);
    const auto sphereOption = std::find_if(
        shapeKind->enumOptions.begin(), shapeKind->enumOptions.end(), [](const auto& option) {
            return option.name == "Sphere";
        });
    assert(sphereOption != shapeKind->enumOptions.end());
    editor::SetSceneComponentPropertyCommand setColliderKind("body.shape.kind");
    setColliderKind.objectId = colliderObject->id().value();
    setColliderKind.componentId = collider->id().value();
    setColliderKind.valueKind = editor::ScenePropertyValueKind::Enumeration;
    setColliderKind.integerValue = sphereOption->value;
    assert(shell.submitCommand(setColliderKind).status == editor::EditorResponseStatus::Completed);
    assert(collider->desc().body.shape.kind == physics::ColliderShapeKind::Sphere);
    const auto stateBeforeCountSentinel = shell.stateVersion();
    auto setColliderLayerSentinel = setColliderKind;
    setColliderLayerSentinel.name = "body.layer";
    setColliderLayerSentinel.integerValue = static_cast<int>(physics::CollisionLayer::Count);
    assert(shell.submitCommand(setColliderLayerSentinel).status
        == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == stateBeforeCountSentinel);
    editor::SetSceneComponentPropertyCommand setColliderRadius("body.shape.radius");
    setColliderRadius.objectId = colliderObject->id().value();
    setColliderRadius.componentId = collider->id().value();
    setColliderRadius.valueKind = editor::ScenePropertyValueKind::Number;
    setColliderRadius.numberValue = 0.75f;
    assert(
        shell.submitCommand(setColliderRadius).status == editor::EditorResponseStatus::Completed);
    assert(collider->desc().body.shape.radius == 0.75f);
    const auto stateBeforeNonEditorField = shell.stateVersion();
    auto setColliderVertices = setColliderRadius;
    setColliderVertices.name = "body.shape.convexPoints";
    assert(shell.submitCommand(setColliderVertices).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == stateBeforeNonEditorField);
    assert(shell.submitCommand(editor::SelectSceneObjectCommand{selected.value()}).status
        == editor::EditorResponseStatus::Completed);

    const auto priorSelection = shell.sceneDocument().scene().objects().front()->id();
    auto destroyed = shell.submitCommand(editor::DestroySceneObjectCommand{});
    assert(destroyed.status == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().scene().findObject(selected) == nullptr);
    auto selectedAgain =
        shell.submitCommand(editor::SelectSceneObjectCommand{priorSelection.value()});
    assert(selectedAgain.status == editor::EditorResponseStatus::Completed);

    {
        editor::EditorSceneDocument authoring;
        authoring.setResourceManagerProvider([&shell] {
            return &shell.projectResources();
        });
        authoring.setPhysicsWorld2DProvider([editorPhysicsWorld] {
            return editorPhysicsWorld;
        });
        authoring.scene().setName(
            core::pmr::String("AuthoringRoundTrip", &core::defaultCpuMemoryResource()));
        const auto parentId = authoring.createObject("Parent");
        const auto childId = authoring.createObject("Child");
        assert(authoring.reparentObject(childId, parentId, 0));
        auto* parent = authoring.scene().findObject(parentId);
        auto transform = parent->transform().localTransform();
        transform.translation = {2.0f, -3.0f, 4.5f};
        parent->transform().setLocalTransform(transform);
        auto* lightObject = authoring.scene().findObjectByName(
            core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
        assert(lightObject);
        auto* light = lightObject->getComponent<scene3d::LightComponent>();
        assert(light);
        auto lightProperties = authoring.inspectorProperties(lightObject->id());
        const auto lightType =
            std::find_if(lightProperties.begin(), lightProperties.end(), [](const auto& property) {
                return property.componentType == "LightComponent" && property.name == "type";
            });
        const auto lightColor =
            std::find_if(lightProperties.begin(), lightProperties.end(), [](const auto& property) {
                return property.componentType == "LightComponent" && property.name == "color";
            });
        assert(lightType != lightProperties.end() && lightColor != lightProperties.end());
        assert(authoring.setComponentProperty(lightObject->id(),
                   light->id(),
                   "type",
                   editor::SceneInspectorPropertyKind::Enumeration,
                   false,
                   static_cast<int>(scene3d::LightType::Spot),
                   0,
                   0.0f)
            == editor::SceneInspectorWriteResult::Changed);
        assert(authoring.setComponentProperty(lightObject->id(),
                   light->id(),
                   "color",
                   editor::SceneInspectorPropertyKind::Vector3,
                   false,
                   0,
                   0,
                   0.0f,
                   {0.2f, 0.4f, 0.8f})
            == editor::SceneInspectorWriteResult::Changed);
        auto* child = authoring.scene().findObject(childId);
        assert(child->create_component(
                   core::pmr::String("SpriteRendererComponent", &core::defaultCpuMemoryResource()))
            != nullptr);
        auto* animation = dynamic_cast<scene2d::SpriteAnimationComponent*>(
            authoring.addComponent("SpriteAnimationComponent", childId));
        assert(animation);
        assert(authoring.setComponentProperty(childId,
                   animation->id(),
                   "animationResourceId",
                   editor::SceneInspectorPropertyKind::ResourceReference,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   editorAnimationId)
            == editor::SceneInspectorWriteResult::Changed);
        auto* authoredParticleEmitter = dynamic_cast<scene2d::ParticleEmitter2DComponent*>(
            authoring.addComponent("ParticleEmitter2DComponent", childId));
        assert(authoredParticleEmitter);
        assert(authoring.setComponentProperty(childId,
                   authoredParticleEmitter->id(),
                   "spriteId",
                   editor::SceneInspectorPropertyKind::ResourceReference,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   tests::spriteA)
            == editor::SceneInspectorWriteResult::Changed);
        assert(authoring.setComponentProperty(childId,
                   authoredParticleEmitter->id(),
                   "velocitySpread",
                   editor::SceneInspectorPropertyKind::Vector2,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   {},
                   {1.5f, 0.25f})
            == editor::SceneInspectorWriteResult::Changed);
        auto* authoredPhysicsBody = dynamic_cast<physics2d::RigidBody2DComponent*>(
            authoring.addComponent("RigidBody2DComponent", childId));
        assert(authoredPhysicsBody && authoredPhysicsBody->handle());
        assert(authoring.setComponentProperty(childId,
                   authoredPhysicsBody->id(),
                   "linearDamping",
                   editor::SceneInspectorPropertyKind::Number,
                   false,
                   0,
                   0,
                   0.33f)
            == editor::SceneInspectorWriteResult::Changed);
        auto* authoredCollider2D = dynamic_cast<physics2d::Collider2DComponent*>(
            authoring.addComponent("Collider2DComponent", childId));
        assert(authoredCollider2D && authoredCollider2D->handle());
        core::pmr::Vector<core::Vec2> authoredColliderPoints{&core::defaultCpuMemoryResource()};
        authoredColliderPoints = {{-2.0f, 0.0f}, {2.0f, 0.0f}, {0.0f, 2.0f}};
        assert(authoring.setComponentProperty(childId,
                   authoredCollider2D->id(),
                   "points",
                   editor::SceneInspectorPropertyKind::Vector2List,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   {},
                   {},
                   authoredColliderPoints)
            == editor::SceneInspectorWriteResult::Changed);
        assert(authoring.setComponentProperty(childId,
                   authoredCollider2D->id(),
                   "type",
                   editor::SceneInspectorPropertyKind::Enumeration,
                   false,
                   static_cast<int>(physics2d::ShapeType::Convex),
                   0,
                   0.0f)
            == editor::SceneInspectorWriteResult::Changed);
        assert(authoring.setComponentProperty(childId,
                   authoredCollider2D->id(),
                   "radius",
                   editor::SceneInspectorPropertyKind::Number,
                   false,
                   0,
                   0,
                   0.8f)
            == editor::SceneInspectorWriteResult::Changed);
        auto* authoredSpriteText = dynamic_cast<scene2d::SpriteTextComponent*>(
            authoring.addComponent("SpriteTextComponent", childId));
        assert(authoredSpriteText);
        assert(authoring.setComponentProperty(childId,
                   authoredSpriteText->id(),
                   "fontResourceId",
                   editor::SceneInspectorPropertyKind::ResourceReference,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   editorSpriteFontId)
            == editor::SceneInspectorWriteResult::Changed);
        assert(authoring.setComponentProperty(childId,
                   authoredSpriteText->id(),
                   "text",
                   editor::SceneInspectorPropertyKind::String,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   "AB")
            == editor::SceneInspectorWriteResult::Changed);
        auto* document = child->create_component(
            core::pmr::String("DocumentComponent", &core::defaultCpuMemoryResource()));
        assert(document);
        auto documentProps = authoring.inspectorProperties(childId);
        const auto sourceProp =
            std::find_if(documentProps.begin(), documentProps.end(), [](const auto& property) {
                return property.componentType == "DocumentComponent"
                    && property.name == "documentSource";
            });
        assert(sourceProp != documentProps.end());
        assert(authoring.setComponentProperty(childId,
                   document->id(),
                   "documentSource",
                   editor::SceneInspectorPropertyKind::String,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   "ui/scenes/child.arpui")
            == editor::SceneInspectorWriteResult::Changed);
        auto* meshObject = authoring.scene().findObjectByName(
            core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()));
        auto* mesh = meshObject->getComponent<scene3d::MeshComponent>();
        assert(mesh);
        assert(authoring.setComponentProperty(meshObject->id(),
                   mesh->id(),
                   "resourceId",
                   editor::SceneInspectorPropertyKind::ResourceReference,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   readyMeshId)
            == editor::SceneInspectorWriteResult::Changed);
        assert(authoring.setComponentProperty(meshObject->id(),
                   mesh->id(),
                   "baseColor",
                   editor::SceneInspectorPropertyKind::Color4,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {0.2f, 0.4f, 0.6f, 0.8f})
            == editor::SceneInspectorWriteResult::Changed);

        auto animationState = animation->editorAuthoringProperties();
        animationState.cast<scene2d::SpriteAnimationEditorProperties&>().speed = 1.23456789012345;
        assert(animation->applyEditorAuthoringProperties(animationState));
        auto filterState = authoredCollider2D->editorAuthoringProperties();
        filterState.cast<physics2d::Collider2DEditorProperties&>().filter.mask = UINT64_MAX;
        assert(authoredCollider2D->applyEditorAuthoringProperties(filterState));
        core::pmr::String yaml(&core::defaultCpuMemoryResource());
        core::pmr::String diagnostic(&core::defaultCpuMemoryResource());
        assert(editor::EditorSceneAuthoringCodec::encode(authoring, yaml, diagnostic));
        editor::EditorSceneDocument loaded;
        loaded.setResourceManagerProvider([&shell] {
            return &shell.projectResources();
        });
        loaded.setPhysicsWorld2DProvider([editorPhysicsWorld] {
            return editorPhysicsWorld;
        });
        const auto activeNameBeforeRejectedResource = loaded.scene().name();
        assert(!editor::EditorSceneAuthoringCodec::decode(
            std::string_view(yaml.data(), yaml.size()), loaded, nullptr, nullptr, diagnostic));
        assert(loaded.scene().name() == activeNameBeforeRejectedResource);
        const auto resourceValidator =
            [](void* context, std::string_view resourceType, std::string_view resourceId) {
                auto* manager = static_cast<resource::ResourceManager*>(context);
                const auto* record = manager->recordSnapshot(resourceId);
                return record && record->state == resource::ResourceState::Ready
                    && std::string_view(record->type.data(), record->type.size()) == resourceType;
            };
        assert(editor::EditorSceneAuthoringCodec::decode(std::string_view(yaml.data(), yaml.size()),
            loaded,
            resourceValidator,
            &shell.projectResources(),
            diagnostic));
        assert(loaded.scene().name() == "AuthoringRoundTrip");
        const auto* loadedParent = loaded.scene().findObjectByName(
            core::pmr::String("Parent", &core::defaultCpuMemoryResource()));
        const auto* loadedChild = loaded.scene().findObjectByName(
            core::pmr::String("Child", &core::defaultCpuMemoryResource()));
        assert(loadedParent && loadedChild && loadedChild->parentId() == loadedParent->id());
        assert(
            loaded.scene().findObject(loadedParent->childIds().front())->id() == loadedChild->id());
        assert(loadedParent->transform().localTransform().translation.x == 2.0f);
        const auto* loadedLightObject = loaded.scene().findObjectByName(
            core::pmr::String("DirectionalLight", &core::defaultCpuMemoryResource()));
        const auto* loadedLight = loadedLightObject->getComponent<scene3d::LightComponent>();
        assert(loadedLight && loadedLight->desc().type == scene3d::LightType::Spot);
        assert(std::abs(loadedLight->desc().color.z - 0.8f) < 0.0001f);
        const auto* loadedMeshObject = loaded.scene().findObjectByName(
            core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()));
        assert(
            loadedMeshObject->getComponent<scene3d::MeshComponent>()->resourceId() == readyMeshId);
        const auto* loadedMesh = loadedMeshObject->getComponent<scene3d::MeshComponent>();
        assert(std::abs(loadedMesh->desc().baseColorTint.x - 0.2f) < 0.0001f
            && std::abs(loadedMesh->desc().baseColorTint.y - 0.4f) < 0.0001f
            && std::abs(loadedMesh->desc().baseColorTint.z - 0.6f) < 0.0001f
            && std::abs(loadedMesh->desc().baseColorTint.w - 0.8f) < 0.0001f);
        const auto loadedDocumentProps = loaded.inspectorProperties(loadedChild->id());
        assert(std::any_of(
            loadedDocumentProps.begin(), loadedDocumentProps.end(), [](const auto& property) {
                return property.componentType == "DocumentComponent"
                    && property.name == "documentSource"
                    && property.stringValue == "ui/scenes/child.arpui";
            }));
        const auto* loadedAnimation =
            loadedChild->getComponent<scene2d::SpriteAnimationComponent>();
        assert(loadedAnimation && loadedAnimation->speed() == 1.23456789012345);
        assert(loadedAnimation && loadedAnimation->valid() && loadedAnimation->animationReady()
            && loadedAnimation->animationResourceId() == editorAnimationId);
        const auto* loadedParticleEmitter =
            loadedChild->getComponent<scene2d::ParticleEmitter2DComponent>();
        assert(loadedParticleEmitter && loadedParticleEmitter->spriteReady()
            && loadedParticleEmitter->sprite().id() == tests::spriteA);
        const auto loadedParticleProperties = loadedParticleEmitter->editorAuthoringProperties();
        const auto& loadedParticleValues =
            loadedParticleProperties.cast<const scene2d::ParticleEmitter2DEditorProperties&>();
        assert(loadedParticleValues.velocitySpread.x == 1.5f
            && loadedParticleValues.velocitySpread.y == 0.25f);
        const auto* loadedPhysicsBody =
            loadedChild->getComponent<physics2d::RigidBody2DComponent>();
        assert(loadedPhysicsBody && loadedPhysicsBody->handle()
            && std::abs(loadedPhysicsBody->desc().linearDamping - 0.33f) < 0.0001f);
        const auto* loadedCollider2D = loadedChild->getComponent<physics2d::Collider2DComponent>();
        assert(loadedCollider2D && loadedCollider2D->handle()
            && loadedCollider2D->desc().type == physics2d::ShapeType::Convex
            && std::abs(loadedCollider2D->desc().radius - 0.8f) < 0.0001f);
        assert(loadedCollider2D->desc().filter.mask == UINT64_MAX);
        assert(loadedCollider2D->desc().points.size() == 3u
            && loadedCollider2D->desc().points[0].x == -2.0f
            && loadedCollider2D->desc().points[1].x == 2.0f
            && loadedCollider2D->desc().points[2].y == 2.0f);
        const auto* loadedSpriteText = loadedChild->getComponent<scene2d::SpriteTextComponent>();
        assert(loadedSpriteText && loadedSpriteText->layout().glyphCount() == 2u);
        const auto loadedSpriteTextProperties = loadedSpriteText->editorAuthoringProperties();
        const auto& loadedSpriteTextValues =
            loadedSpriteTextProperties.cast<const scene2d::SpriteTextEditorProperties&>();
        assert(loadedSpriteTextValues.fontResourceId == editorSpriteFontId
            && loadedSpriteTextValues.text == "AB");

        const auto preservedName = loaded.scene().name();
        auto invalidYaml = core::pmr::String(yaml, &core::defaultCpuMemoryResource());
        const auto versionToken = invalidYaml.find("contractVersion: 2");
        assert(versionToken != core::pmr::String::npos);
        invalidYaml.replace(versionToken, sizeof("contractVersion: 2") - 1u, "contractVersion: 99");
        assert(!editor::EditorSceneAuthoringCodec::decode(
            std::string_view(invalidYaml.data(), invalidYaml.size()),
            loaded,
            nullptr,
            nullptr,
            diagnostic));
        assert(loaded.scene().name() == preservedName);
        assert(loaded.scene().findObjectByName(
            core::pmr::String("Parent", &core::defaultCpuMemoryResource())));

        const auto persistenceRoot = std::filesystem::temp_directory_path()
            / ("arp-editor-scene-persistence-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(persistenceRoot / "scenes");
        {
            editor::EditorScenePersistence persistence;
            assert(!persistence.save(persistenceRoot, "../outside.arp.yaml", yaml));
            assert(std::string_view(persistence.diagnostic()) == "EDITOR-SCENE-PATH-UNSAFE");

            const auto finish = [&](editor::EditorScenePersistence::RequestHandle handle,
                                    core::io::AtomicFileResult& result) {
                assert(handle);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (std::chrono::steady_clock::now() < deadline) {
                    if (persistence.take(handle, result))
                        return;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                assert(false && "scene persistence request did not complete");
            };
            core::io::AtomicFileResult saved;
            finish(persistence.save(persistenceRoot, "scenes/test.arp.yaml", yaml), saved);
            if (!saved.success)
                std::fprintf(stderr, "scene save rejected: %s\n", saved.diagnostic.c_str());
            assert(saved.success && saved.committed);

            core::io::AtomicFileResult loadedFile;
            finish(persistence.load(persistenceRoot, "scenes/test.arp.yaml"), loadedFile);
            assert(loadedFile.success);
            assert(std::string_view(reinterpret_cast<const char*>(loadedFile.bytes.data()),
                       loadedFile.bytes.size())
                == std::string_view(yaml.data(), yaml.size()));

            auto invalidReplacement = yaml;
            const auto replacementVersion = invalidReplacement.find("contractVersion: 2");
            assert(replacementVersion != core::pmr::String::npos);
            invalidReplacement.replace(
                replacementVersion, sizeof("contractVersion: 2") - 1u, "contractVersion: 99");
            core::io::AtomicFileResult invalidSave;
            finish(persistence.save(persistenceRoot, "scenes/test.arp.yaml", invalidReplacement),
                invalidSave);
            assert(!invalidSave.success && !invalidSave.committed);
            core::io::AtomicFileResult stillLoaded;
            finish(persistence.load(persistenceRoot, "scenes/test.arp.yaml"), stillLoaded);
            assert(stillLoaded.success);
            assert(std::string_view(reinterpret_cast<const char*>(stillLoaded.bytes.data()),
                       stillLoaded.bytes.size())
                == std::string_view(yaml.data(), yaml.size()));

            {
                std::ofstream corrupt(persistenceRoot / "scenes/corrupt.arp.yaml",
                    std::ios::binary | std::ios::trunc);
                corrupt << "type: invalid\n";
            }
            core::io::AtomicFileResult rejected;
            finish(persistence.save(persistenceRoot, "scenes/corrupt.arp.yaml", yaml), rejected);
            assert(!rejected.success && !rejected.committed);
            std::ifstream preserved(persistenceRoot / "scenes/corrupt.arp.yaml", std::ios::binary);
            std::string preservedText((std::istreambuf_iterator<char>(preserved)), {});
            assert(preservedText == "type: invalid\n");
        }
        std::filesystem::remove_all(persistenceRoot);
    }

    {
        physics2d::WorldConfig config{{0, -9.8f}, 32, 64, 16, 128, 256, 128, 32, 4, 1.0f / 60.0f};
        auto sourceWorld = core::makeShared<physics2d::PhysicsWorld2D>(config);
        editor::EditorSceneDocument source(false);
        source.setPhysicsWorld2DProvider([sourceWorld] {
            return sourceWorld;
        });
        const auto discarded = source.createObject("discarded");
        assert(source.scene().destroyObject(discarded).ok());
        const auto jointOwnerId = source.createObject("Joint");
        const auto bodyAId = source.createObject("Body A");
        const auto bodyBId = source.createObject("Body B");
        const auto bodyCId = source.createObject("Body C");
        assert(source.addComponent("RigidBody2DComponent", bodyAId));
        assert(source.addComponent("RigidBody2DComponent", bodyBId));
        assert(source.addComponent("RigidBody2DComponent", bodyCId));
        auto* joint = dynamic_cast<physics2d::Joint2DComponent*>(
            source.addComponent("Joint2DComponent", jointOwnerId));
        assert(joint && !joint->handle());

        auto properties = source.inspectorProperties(jointOwnerId);
        const auto jointProperties = [&](std::string_view name) -> const auto* {
            const auto found =
                std::find_if(properties.begin(), properties.end(), [&](const auto& property) {
                    return property.componentId == joint->id().value() && property.name == name;
                });
            return found == properties.end() ? nullptr : &*found;
        };
        const auto* bodyAProperty = jointProperties("bodyA");
        const auto* bodyBProperty = jointProperties("bodyB");
        assert(bodyAProperty && bodyBProperty
            && bodyAProperty->kind == editor::SceneInspectorPropertyKind::SceneObjectReference
            && bodyBProperty->kind == editor::SceneInspectorPropertyKind::SceneObjectReference
            && bodyAProperty->sceneObjectType == "RigidBody2DComponent");
        assert(source.setComponentProperty(jointOwnerId,
                   joint->id(),
                   "bodyA",
                   editor::SceneInspectorPropertyKind::SceneObjectReference,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   {},
                   {},
                   {},
                   bodyAId.value())
            == editor::SceneInspectorWriteResult::Changed);
        assert(source.setComponentProperty(jointOwnerId,
                   joint->id(),
                   "bodyB",
                   editor::SceneInspectorPropertyKind::SceneObjectReference,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   {},
                   {},
                   {},
                   bodyBId.value())
            == editor::SceneInspectorWriteResult::Changed);
        assert(joint->handle() && joint->bodyAObjectId() == bodyAId.value()
            && joint->bodyBObjectId() == bodyBId.value());
        const auto firstJointHandle = joint->handle();
        assert(source.setComponentProperty(jointOwnerId,
                   joint->id(),
                   "bodyB",
                   editor::SceneInspectorPropertyKind::SceneObjectReference,
                   false,
                   0,
                   0,
                   0.0f,
                   {},
                   {},
                   {},
                   {},
                   {},
                   bodyCId.value())
            == editor::SceneInspectorWriteResult::Changed);
        assert(joint->handle() && !(joint->handle() == firstJointHandle)
            && joint->bodyAObjectId() == bodyAId.value()
            && joint->bodyBObjectId() == bodyCId.value() && sourceWorld->telemetry().joints == 1u);
        auto* colliderA = dynamic_cast<physics2d::Collider2DComponent*>(
            source.addComponent("Collider2DComponent", bodyAId));
        assert(colliderA && colliderA->handle());
        const auto physicsOverlay =
            editor::buildEditorScenePhysicsOverlay(source.scene(), 128u, 16u);
        assert(physicsOverlay.valid && physicsOverlay.segments.size() == 17u);
        assert(std::count_if(physicsOverlay.segments.begin(),
                   physicsOverlay.segments.end(),
                   [](const auto& segment) {
                       return segment.kind == editor::EditorScenePhysicsOverlayKind::Joint;
                   })
            == 1);
        const auto exceededPhysicsOverlay =
            editor::buildEditorScenePhysicsOverlay(source.scene(), 8u, 16u);
        assert(!exceededPhysicsOverlay.valid
            && std::string_view(exceededPhysicsOverlay.diagnostic)
                == "ARP-EDITOR-PHYSICS-OVERLAY-SEGMENT-BUDGET-EXCEEDED");
        const auto verifyShape = [&](physics2d::ShapeType type, std::size_t segmentCount) {
            auto properties = colliderA->editorAuthoringProperties()
                                  .cast<const physics2d::Collider2DEditorProperties&>();
            properties.type = type;
            properties.points.clear();
            if (type == physics2d::ShapeType::Box)
                properties.halfSize = {1.0f, 0.5f};
            if (type == physics2d::ShapeType::Capsule) {
                properties.pointA = {-1.0f, 0.0f};
                properties.pointB = {1.0f, 0.0f};
                properties.radius = 0.5f;
            }
            if (type == physics2d::ShapeType::Convex) {
                properties.points.push_back({-1.0f, -0.5f});
                properties.points.push_back({1.0f, -0.5f});
                properties.points.push_back({0.0f, 1.0f});
            }
            if (type == physics2d::ShapeType::StaticChain) {
                properties.points.push_back({-1.0f, -0.5f});
                properties.points.push_back({1.0f, -0.5f});
                properties.points.push_back({1.0f, 0.5f});
                properties.points.push_back({-1.0f, 0.5f});
            }
            assert(colliderA->applyEditorAuthoringProperties(reflection::object(properties)));
            const auto overlay = editor::buildEditorScenePhysicsOverlay(source.scene(), 128u, 16u);
            assert(overlay.valid && overlay.segments.size() == segmentCount + 1u);
        };
        verifyShape(physics2d::ShapeType::Box, 4u);
        verifyShape(physics2d::ShapeType::Capsule, 16u);
        verifyShape(physics2d::ShapeType::Convex, 3u);
        verifyShape(physics2d::ShapeType::StaticChain, 3u);
        const auto bodyB = source.scene().findObject(bodyBId);
        assert(bodyB);
        assert(source.removeComponent(
            bodyBId, bodyB->getComponent<physics2d::RigidBody2DComponent>()->id()));
        assert(source.select(bodyAId) && !source.destroySelected()
            && source.scene().findObject(bodyAId));

        core::pmr::String yaml{&core::defaultCpuMemoryResource()};
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        assert(editor::EditorSceneAuthoringCodec::encode(source, yaml, diagnostic));
        const auto encodedScene = core::YamlContract::parseSingleDocument(yaml);
        bool foundObjectReference = false;
        using Node = core::YamlContract::Node;
        for (const auto& object :
            encodedScene["objects"].get_value_ref<const Node::sequence_type&>())
            for (const auto& component :
                object["components"].get_value_ref<const Node::sequence_type&>())
                for (const auto& property :
                    component["properties"].get_value_ref<const Node::mapping_type&>())
                    foundObjectReference = foundObjectReference
                        || core::YamlContract::text(property.second, "kind")
                            == "sceneObjectReference";
        assert(foundObjectReference);
        auto destinationWorld = core::makeShared<physics2d::PhysicsWorld2D>(config);
        editor::EditorSceneDocument destination(false);
        destination.setPhysicsWorld2DProvider([destinationWorld] {
            return destinationWorld;
        });
        assert(editor::EditorSceneAuthoringCodec::decode(
            std::string_view(yaml.data(), yaml.size()), destination, nullptr, nullptr, diagnostic));
        const auto& loadedObjects = destination.scene().objects();
        physics2d::Joint2DComponent* loadedJoint = nullptr;
        std::uint64_t loadedBodyA = 0;
        std::uint64_t loadedBodyB = 0;
        for (const auto& object : loadedObjects) {
            if (auto* body = object->getComponent<physics2d::RigidBody2DComponent>()) {
                if (object->name() == "Body A")
                    loadedBodyA = object->id().value();
                else if (object->name() == "Body C")
                    loadedBodyB = object->id().value();
            }
            if (auto* candidate = object->getComponent<physics2d::Joint2DComponent>())
                loadedJoint = candidate;
        }
        assert(loadedJoint && loadedJoint->handle() && loadedBodyA != bodyAId.value()
            && loadedJoint->bodyAObjectId() == loadedBodyA && loadedBodyB != bodyCId.value()
            && loadedJoint->bodyBObjectId() == loadedBodyB
            && destinationWorld->telemetry().joints == 1u);
        assert(!source.removeComponent(bodyAId,
            source.scene()
                .findObject(bodyAId)
                ->getComponent<physics2d::RigidBody2DComponent>()
                ->id()));
        const auto staleJointHandle = joint->handle();
        assert(source.removeComponent(jointOwnerId, joint->id())
            && sourceWorld->telemetry().joints == 0u
            && !sourceWorld->destroyJoint(staleJointHandle));
    }

    auto drawn = shell.drawCommands();
    assert(drawn.ok());
    return 0;
}

#include "editor/app/editor_application.h"

#include "2d/particle_emitter_component.h"
#include "2d/sprite_text_component.h"
#include "3d/authoring_components.h"
#include "3d/components.h"
#include "editor/app/game_preview.h"
#include "editor/remote/editor_remote_server.h"
#ifdef DeviceCapabilities
#undef DeviceCapabilities
#endif
#include "2d/scene_runtime_codec.h"
#include "editor/ui/editor_ui_assets.h"
#include "editor/viewport/editor_scene_component_gizmos.h"
#include "editor/viewport/editor_scene_viewport_frame_builder.h"
#include "editor/viewport/editor_scene_viewport_resource_resolver.h"
#include "editor/viewport/editor_scene_viewport_session.h"
#include "editor/workspace/editor_layout.h"
#include "editor/workspace/editor_shell.h"
#include "physics2d/components.h"
#include "platform/input_bridge.h"
#include "platform/render_system.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/ui/gui_renderer.h"
#include "resource/mesh_asset_resources.h"
#include "resource/prefab_asset_resources.h"
#include "scene/camera.h"
#include "scene/camera_temporal_component.h"
#include "scene/runtime_components.h"
#include "scene/scene_asset_resource.h"
#include "tests/fixtures/terrain_gizmo_fixture.h"
#include "tests/fixtures/tile_map_fixture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <thread>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <renderdoc_app.h>
#include <windows.h>
#endif
namespace arp::editor {
namespace {
struct ScopedTemporaryDirectory {
    std::filesystem::path path;

    ~ScopedTemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

int fail(const char* stage, const core::pmr::String& message)
{
    std::fprintf(stderr, "EDITOR-%s: %s\n", stage, message.c_str());
    return 1;
}

constexpr const char* kViewportSmokeMeshId = "7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b1";
constexpr const char* kViewportSmokeMaterialId = "c8591a23-0f99-4070-a4d0-d79ab87379eb";
constexpr const char* kViewportSmokeCubeMeshId = "7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b2";
constexpr const char* kViewportSmokeCubeMaterialId = "c8591a23-0f99-4070-a4d0-d79ab87379ec";
constexpr const char* kViewportSmokeSourceId = "b055987e-d04c-42ef-a3f1-f77721eac8f7";
constexpr const char* kViewportSmokeShaderId = "9e3f0b60-93f7-42b8-88e7-32e0c9f0bc40";
constexpr const char* kViewportSmokeSrgbTextureId = "86ccf943-c70e-4ddc-b7c4-f1a6c02ff115";
constexpr const char* kViewportSmokeLinearTextureId = "fd0155f8-360c-43d3-bdb3-081a9848c1a7";

resource::ResourceReference viewportSmokeReference(const char* id, const char* type)
{
    resource::ResourceReference reference;
    reference.id = id;
    reference.type = type;
    reference.status = "Contract Complete";
    return reference;
}

core::UniquePtr<resource::TextureResource> viewportSmokeTexture(const char* id, bool srgb)
{
    auto texture = core::makeUnique<resource::TextureResource>();
    texture->configureMetadata(viewportSmokeReference(id, "TextureResource"));
    texture->usage = srgb ? "color" : "data";
    texture->srgb = srgb;
    texture->decodedWidth = 1u;
    texture->decodedHeight = 1u;
    texture->decodedSrgb = srgb;
    texture->decodedRgba8 = {255u, 255u, 255u, 255u};
    return texture;
}

core::UniquePtr<resource::MaterialResource> viewportSmokeMaterial(
    const char* id, float red, float green, float blue, float metallic, float roughness)
{
    auto material = core::makeUnique<resource::MaterialResource>();
    material->configureMetadata(viewportSmokeReference(id, "MaterialResource"));
    material->sourceResourceId = kViewportSmokeSourceId;
    material->shaderPackageId = kViewportSmokeShaderId;
    material->materialContractVersion = resource::kPbrMetallicRoughnessContractVersion;
    material->surfaceModel = resource::kPbrMetallicRoughnessContract;
    material->baseColorR = red;
    material->baseColorG = green;
    material->baseColorB = blue;
    material->alphaMode = "OPAQUE";
    material->metallicFactor = metallic;
    material->roughnessFactor = roughness;
    material->baseColorTextureId = kViewportSmokeSrgbTextureId;
    material->normalTextureId = kViewportSmokeLinearTextureId;
    material->metallicRoughnessTextureId = kViewportSmokeLinearTextureId;
    material->occlusionTextureId = kViewportSmokeLinearTextureId;
    material->emissiveTextureId = kViewportSmokeSrgbTextureId;
    return material;
}

bool installViewportSmokeSceneResources(EditorShell& shell, core::pmr::String& diagnostics)
{
    auto mesh = core::makeUnique<resource::MeshAssetResource>(
        core::pmr::String(kViewportSmokeMeshId, &core::defaultCpuMemoryResource()),
        assets::MeshAsset::debugTriangle());
    if (!shell.projectResources().loadResource(std::move(mesh)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-MESH-RESOURCE";
        return false;
    }
    auto cubeMesh = core::makeUnique<resource::MeshAssetResource>(
        core::pmr::String(kViewportSmokeCubeMeshId, &core::defaultCpuMemoryResource()), 0.8f);
    if (!shell.projectResources().loadResource(std::move(cubeMesh)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-CUBE-MESH-RESOURCE";
        return false;
    }

    auto source = core::makeUnique<resource::ExternalAssetSourceResource>();
    source->configureMetadata(
        viewportSmokeReference(kViewportSmokeSourceId, "ExternalAssetSourceResource"));
    if (!shell.projectResources().loadResource(std::move(source)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-SOURCE-RESOURCE";
        return false;
    }

    auto shader = core::makeUnique<resource::ShaderPackageResource>();
    shader->configureMetadata(
        viewportSmokeReference(kViewportSmokeShaderId, "ShaderPackageResource"));
    shader->slangPath = "shaders/editor/viewport_pbr.slang";
    shader->spirvPath = "shaders/editor/viewport_pbr.spv";
    shader->mslPath = "shaders/editor/viewport_pbr.metal";
    shader->reflectionPath = "shaders/editor/viewport_pbr.reflection.yaml";
    shader->materialInterfaceContract = resource::kPbrMetallicRoughnessContract;
    shader->materialInterfaceVersion = resource::kPbrMetallicRoughnessContractVersion;
    if (!shell.projectResources().loadResource(std::move(shader)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-SHADER-RESOURCE";
        return false;
    }

    if (!shell.projectResources()
            .loadResource(
                viewportSmokeMaterial(kViewportSmokeMaterialId, 0.8f, 0.32f, 0.12f, 0.0f, 0.8f))
            .success
        || !shell.projectResources()
            .loadResource(viewportSmokeMaterial(
                kViewportSmokeCubeMaterialId, 0.12f, 0.42f, 0.82f, 0.15f, 0.35f))
            .success
        || !shell.projectResources()
            .loadResource(viewportSmokeTexture(kViewportSmokeSrgbTextureId, true))
            .success
        || !shell.projectResources()
            .loadResource(viewportSmokeTexture(kViewportSmokeLinearTextureId, false))
            .success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-MATERIAL-RESOURCES";
        return false;
    }
    return true;
}
} // namespace

int EditorApplication::run(bool hidden,
    bool after,
    bool hover,
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& openScript,
    std::uint16_t remotePort,
    std::string_view remoteBindAddress,
    bool viewportPipelineSmoke,
    bool viewportRuntimeSmoke)
{
    using namespace arp;
    editor::EditorShell shell;
    ScopedTemporaryDirectory terrainGizmoFixtureDirectory;
    const auto root = std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources";
    auto loaded = shell.load(root / "editor.arpui", projectRoot);
    if (!loaded.ok())
        return fail("LOAD", loaded.diagnostic().message);
    scene::GameObjectId tileMapCaptureObject;
    if (!openScript.empty()) {
        const auto scriptPath =
            openScript.is_absolute() ? openScript : shell.projectRoot() / openScript;
        if (!shell.openScriptFile(scriptPath))
            return fail("SCRIPT", shell.diagnostic().message);
    }
    EditorRemoteServer remoteServer;
    if (remotePort != 0) {
        const auto* token = std::getenv("ARP_EDITOR_REMOTE_TOKEN");
        if (!token || !remoteServer.start(shell, token, remotePort, remoteBindAddress))
            return fail("REMOTE",
                token ? remoteServer.diagnostic()
                      : core::pmr::String("ARP_EDITOR_REMOTE_TOKEN is required"));
        std::printf("ARP Editor remote API and MCP listening on %.*s:%u (%s)\n",
            static_cast<int>(remoteBindAddress.size()),
            remoteBindAddress.data(),
            remoteServer.port(),
            EditorRemoteServer::isLoopbackAddress(remoteBindAddress) ? "loopback" : "non-loopback");
    }
    if (after) {
        if (!shell.scenario().ok())
            return fail("SCENARIO", "ordered input failed");
        shell.activate("dock_Signals");
        shell.activate("dock_Groups");
        shell.activate("dock_Inspector");
        shell.activate("bottom_Output");
        shell.activate("workspace_3D");
        if (shell.submitCommand(CreateSceneObjectCommand("HierarchySmokeParent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot create a hierarchy parent");
        const auto hierarchyParentId = shell.sceneDocument().selectedObject();
        if (shell.submitCommand(CreateSceneObjectCommand("HierarchySmokeChild")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot create a hierarchy child");
        const auto hierarchyChildId = shell.sceneDocument().selectedObject();
        if (!shell.sceneDocument().scene().reparent(hierarchyChildId, hierarchyParentId).ok())
            return fail("SCENE-HIERARCHY-SMOKE", "cannot establish a nested hierarchy");
        const auto hierarchy = shell.sceneDocument().hierarchyEntries();
        const auto childRow =
            std::find_if(hierarchy.begin(), hierarchy.end(), [&](const auto& entry) {
                return entry.objectId == hierarchyChildId;
            });
        const auto parentRow =
            std::find_if(hierarchy.begin(), hierarchy.end(), [&](const auto& entry) {
                return entry.objectId == hierarchyParentId;
            });
        if (parentRow == hierarchy.end() || childRow != parentRow + 1
            || childRow->depth != parentRow->depth + 1u)
            return fail("SCENE-HIERARCHY-SMOKE", "depth-first hierarchy order is incorrect");
        platform::WindowInputEvent hierarchyClick;
        hierarchyClick.kind = platform::WindowInputEvent::Kind::MouseButtonDown;
        hierarchyClick.mouseButton = platform::WindowMouseButton::Left;
        hierarchyClick.x = layout::kSceneHierarchyLeft + 12.0f;
        hierarchyClick.y = layout::kSceneHierarchyTop
            + static_cast<float>(std::distance(hierarchy.begin(), childRow))
                * layout::kSceneHierarchyRowHeight
            + layout::kSceneHierarchyRowHeight * 0.5f;
        shell.handlePlatformInput(hierarchyClick);
        if (shell.sceneDocument().selectedObject() != hierarchyChildId)
            return fail(
                "SCENE-HIERARCHY-SMOKE", "hierarchy row did not select the matching GameObject");
        auto* transformObject =
            shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
        auto* transformComponent =
            transformObject ? transformObject->getComponent<scene::TransformComponent>() : nullptr;
        if (!transformObject || !transformComponent)
            return fail("SCENE-SMOKE", "selected GameObject has no required TransformComponent");
        core::pmr::String positionSource("scene_property_", &core::defaultCpuMemoryResource());
        positionSource += std::to_string(transformObject->id().value());
        positionSource.push_back('_');
        positionSource += std::to_string(transformComponent->id().value());
        positionSource += "_position_x";
        ui::WidgetId positionInput;
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == positionSource)
                positionInput = widgetId;
        }
        if (!positionInput.valid()
            || !shell.runtime()
                .interact(ui::UiControlInteraction::editText(
                    positionInput, core::pmr::String("1.25", &core::defaultCpuMemoryResource())))
                .ok())
            return fail(
                "SCENE-SMOKE", "cannot edit Transform through its reflected Inspector field");
        transformObject = shell.sceneDocument().scene().findObject(transformObject->id());
        if (!transformObject
            || transformObject->transform().localTransform().translation.x != 1.25f)
            return fail(
                "SCENE-SMOKE", "reflected Transform field did not commit the object transform");
        if (shell.submitCommand(AddSceneComponentCommand("WaterSurfaceComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-SMOKE", "cannot add catalog component");
        if (shell.submitCommand(AddSceneComponentCommand("ColliderComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-SMOKE", "cannot add collider authoring component");
        if (shell.submitCommand(AddSceneComponentCommand("SpriteRendererComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-SMOKE", "cannot add sprite authoring component");
        if (shell.submitCommand(AddSceneComponentCommand("TileMapComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-SMOKE", "cannot add tilemap authoring component");
        if (shell.submitCommand(AddSceneComponentCommand("ResourceRequestComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-SMOKE", "cannot add resource request component");
        if (shell.submitCommand(AddSceneComponentCommand("AudioSourceComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-SMOKE", "cannot add audio source authoring component");
        if (shell.submitCommand(AddSceneComponentCommand("DocumentComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SCENE-SMOKE", "cannot add UI document authoring component");
        auto* sceneObject =
            shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
        auto* document = sceneObject ? sceneObject->getComponent<ui::DocumentComponent>() : nullptr;
        if (!document)
            return fail("SCENE-SMOKE", "UI document component is absent");
        const auto documentPropertyWidget = [&](const char* field) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(sceneObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(document->id().value());
            widgetName.push_back('_');
            widgetName += field;
            for (const auto widgetId : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(widgetId);
                if (widget && widget->name() == widgetName)
                    return widgetId;
            }
            return ui::WidgetId::invalid();
        };
        const auto sourceWidget = documentPropertyWidget("documentSource");
        if (!sourceWidget.valid()
            || !shell.runtime()
                .interact(ui::UiControlInteraction::editText(sourceWidget,
                    core::pmr::String(
                        "editor/resources/editor_sample.arpui", &core::defaultCpuMemoryResource())))
                .ok())
            return fail(
                "SCENE-SMOKE", "cannot author UI document source through its Inspector field");
        if (document->documentSource() != "editor/resources/editor_sample.arpui")
            return fail(
                "SCENE-SMOKE", "Inspector text input did not commit the UI document source");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("SCENE-SMOKE", "cannot scroll to 2D authoring properties");
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto* propertyView =
            layout.ok() ? layout.findWidget(shell.id("properties")) : nullptr;
        if (!propertyView)
            return fail("SCENE-SMOKE", "cannot resolve UI document Inspector viewport");
        for (const auto* field : {"documentSource", "layer"}) {
            const auto fieldWidget = documentPropertyWidget(field);
            const auto* fieldLayout =
                fieldWidget.valid() ? layout.findWidget(fieldWidget) : nullptr;
            if (!fieldLayout || fieldLayout->rect.y < propertyView->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > propertyView->rect.y + propertyView->rect.height)
                return fail(
                    "SCENE-SMOKE", "UI document Inspector field is absent or outside the viewport");
        }
        if (!shell.runtime()
                .interact(
                    ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, -5000.0f))
                .ok())
            return fail("SCENE-SMOKE", "cannot return to the top of the scene Inspector");
        ui::WidgetId currentPositionInput;
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == positionSource)
                currentPositionInput = widgetId;
        }
        const auto topLayout = shell.runtime().canvas().resolveLayout();
        const auto* topPropertyView =
            topLayout.ok() ? topLayout.findWidget(shell.id("properties")) : nullptr;
        const auto* transformInputLayout =
            topLayout.ok() ? topLayout.findWidget(currentPositionInput) : nullptr;
        if (!topPropertyView || !currentPositionInput.valid() || !transformInputLayout
            || transformInputLayout->rect.y < topPropertyView->rect.y
            || transformInputLayout->rect.y + transformInputLayout->rect.height
                > topPropertyView->rect.y + topPropertyView->rect.height)
            return fail("SCENE-SMOKE",
                "Transform input is absent or outside the Inspector viewport at scroll origin");

        constexpr const char* pickerMeshId = "3d573bc0-6c06-41aa-8d58-85f94e8b283a";
        auto pickerMesh = core::makeUnique<resource::MeshAssetResource>(
            core::pmr::String(pickerMeshId, &core::defaultCpuMemoryResource()),
            assets::MeshAsset::debugTriangle());
        if (!shell.projectResources().loadResource(std::move(pickerMesh)).success)
            return fail("RESOURCE-PICKER-SMOKE", "cannot load deterministic Ready mesh fixture");
        const auto* meshObject = shell.sceneDocument().scene().findObjectByName(
            core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()));
        if (!meshObject
            || shell.submitCommand(SelectSceneObjectCommand{meshObject->id().value()}).status
                != EditorResponseStatus::Completed)
            return fail("RESOURCE-PICKER-SMOKE", "cannot select the Mesh fixture GameObject");
        const auto meshProperties = shell.sceneDocument().inspectorProperties(meshObject->id());
        const auto meshResource =
            std::find_if(meshProperties.begin(), meshProperties.end(), [](const auto& property) {
                return property.name == "resourceId"
                    && property.kind == SceneInspectorPropertyKind::ResourceReference
                    && property.editable;
            });
        if (meshResource == meshProperties.end())
            return fail("RESOURCE-PICKER-SMOKE",
                "Mesh resource reference is absent from Inspector metadata");
        const auto meshColor =
            std::find_if(meshProperties.begin(), meshProperties.end(), [](const auto& property) {
                return property.name == "baseColor"
                    && property.kind == SceneInspectorPropertyKind::Color4 && property.editable;
            });
        if (meshColor == meshProperties.end())
            return fail("COLOR-PICKER-SMOKE", "Mesh RGBA color metadata is absent");
        core::pmr::String colorControlName("scene_property_", &core::defaultCpuMemoryResource());
        colorControlName += std::to_string(meshColor->objectId);
        colorControlName.push_back('_');
        colorControlName += std::to_string(meshColor->componentId);
        colorControlName += "_baseColor";
        ui::WidgetId colorControlId;
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == colorControlName
                && widget->desc().controlType == ui::UiControlType::ColorPicker)
                colorControlId = widgetId;
        }
        if (!colorControlId.valid()
            || !shell.runtime()
                .interact(
                    ui::UiControlInteraction::pickColor(colorControlId, 0.2f, 0.4f, 0.6f, 0.8f))
                .ok())
            return fail("COLOR-PICKER-SMOKE", "Mesh RGBA control did not accept a color event");
        const auto* meshComponent = meshObject->getComponent<scene3d::MeshComponent>();
        if (!meshComponent || meshComponent->desc().baseColorTint.x != 0.2f
            || meshComponent->desc().baseColorTint.y != 0.4f
            || meshComponent->desc().baseColorTint.z != 0.6f
            || meshComponent->desc().baseColorTint.w != 0.8f)
            return fail("COLOR-PICKER-SMOKE", "RGBA event did not update the Mesh authoring value");
        core::pmr::String resourceControlName("scene_property_", &core::defaultCpuMemoryResource());
        resourceControlName += std::to_string(meshResource->objectId);
        resourceControlName.push_back('_');
        resourceControlName += std::to_string(meshResource->componentId);
        resourceControlName += "_resourceId";
        ui::WidgetId resourceControlId;
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (widget && widget->name() == resourceControlName
                && widget->desc().controlType == ui::UiControlType::Dropdown)
                resourceControlId = widgetId;
        }
        if (!resourceControlId.valid()
            || !shell.runtime().interact(ui::UiControlInteraction::click(resourceControlId)).ok())
            return fail("RESOURCE-PICKER-SMOKE", "cannot open the typed Mesh resource picker");
        const auto choiceName = resourceControlName + "_option_1";
        bool hasReadyMeshChoice = false;
        ui::WidgetId readyMeshChoiceId;
        for (const auto widgetId : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(widgetId);
            if (!widget || widget->name() != choiceName
                || widget->parentId() != shell.id("inspector_content"))
                continue;
            hasReadyMeshChoice =
                std::string_view(widget->desc().text.data(), widget->desc().text.size())
                    .find(pickerMeshId)
                != std::string_view::npos;
            if (hasReadyMeshChoice)
                readyMeshChoiceId = widgetId;
            break;
        }
        if (!hasReadyMeshChoice)
            return fail("RESOURCE-PICKER-SMOKE",
                "Ready typed Mesh candidate is not visible in picker menu");
        const auto pickerLayout = shell.runtime().canvas().resolveLayout();
        const auto* readyChoiceLayout =
            pickerLayout.ok() ? pickerLayout.findWidget(readyMeshChoiceId) : nullptr;
        const auto choiceClip =
            pickerLayout.ok() ? pickerLayout.clipRect(readyMeshChoiceId) : ui::LayoutRect{};
        if (!readyChoiceLayout || !readyChoiceLayout->visibleInHierarchy
            || choiceClip.width < readyChoiceLayout->rect.width
            || choiceClip.height < readyChoiceLayout->rect.height)
            return fail("RESOURCE-PICKER-SMOKE",
                "Ready Mesh picker option is clipped from the visible Inspector");

        for (const auto* name : {"HierarchyVisualA", "HierarchyVisualB", "HierarchyVisualC"})
            if (shell.submitCommand(CreateSceneObjectCommand(name)).status
                != EditorResponseStatus::Completed)
                return fail("SCENE-HIERARCHY-SMOKE", "cannot create sibling drag fixture");
        const auto hierarchyVisualC = shell.sceneDocument().selectedObject();
        const auto hierarchyBeforeDrag = shell.sceneDocument().hierarchyEntries();
        const auto findHierarchyRow = [&](scene::GameObjectId objectId) {
            return std::find_if(
                hierarchyBeforeDrag.begin(), hierarchyBeforeDrag.end(), [&](const auto& entry) {
                    return entry.objectId == objectId;
                });
        };
        const auto hierarchyVisualA = std::find_if(shell.sceneDocument().scene().objects().begin(),
            shell.sceneDocument().scene().objects().end(),
            [](const auto& object) {
                return object->name() == "HierarchyVisualA";
            });
        const auto hierarchyVisualB = std::find_if(shell.sceneDocument().scene().objects().begin(),
            shell.sceneDocument().scene().objects().end(),
            [](const auto& object) {
                return object->name() == "HierarchyVisualB";
            });
        if (hierarchyVisualA == shell.sceneDocument().scene().objects().end()
            || hierarchyVisualB == shell.sceneDocument().scene().objects().end())
            return fail("SCENE-HIERARCHY-SMOKE", "sibling drag fixtures are missing");
        const auto hierarchyVisualAId = (*hierarchyVisualA)->id();
        const auto hierarchyVisualBId = (*hierarchyVisualB)->id();
        const auto hierarchyRowY = [&](scene::GameObjectId objectId) {
            const auto row = findHierarchyRow(objectId);
            if (row == hierarchyBeforeDrag.end())
                return -1.0f;
            return layout::kSceneHierarchyTop
                + static_cast<float>(row - hierarchyBeforeDrag.begin())
                * layout::kSceneHierarchyRowHeight
                + layout::kSceneHierarchyRowHeight * 0.5f;
        };
        const float hierarchyVisualAY = hierarchyRowY(hierarchyVisualAId);
        const float hierarchyVisualBY = hierarchyRowY(hierarchyVisualBId);
        if (hierarchyVisualAY < 0.0f || hierarchyVisualBY < 0.0f)
            return fail("SCENE-HIERARCHY-SMOKE", "sibling rows are not present in the Hierarchy");
        EditorInputCommand hierarchyDown;
        hierarchyDown.kind = EditorInputKind::PointerDown;
        hierarchyDown.button = EditorPointerButton::Left;
        hierarchyDown.primaryButton = true;
        hierarchyDown.x = layout::kSceneHierarchyLeft + 16.0f;
        hierarchyDown.y = hierarchyVisualAY;
        if (shell.submitCommand(hierarchyDown).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot start Hierarchy drag");
        EditorInputCommand hierarchyMove;
        hierarchyMove.kind = EditorInputKind::PointerDelta;
        hierarchyMove.x = hierarchyDown.x;
        hierarchyMove.y = hierarchyVisualBY + layout::kSceneHierarchyRowHeight * 0.8f;
        hierarchyMove.deltaY = hierarchyMove.y - hierarchyDown.y;
        if (shell.submitCommand(hierarchyMove).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot move Hierarchy drag");
        EditorInputCommand hierarchyUp;
        hierarchyUp.kind = EditorInputKind::PointerUp;
        hierarchyUp.button = EditorPointerButton::Left;
        hierarchyUp.primaryButton = true;
        hierarchyUp.x = hierarchyMove.x;
        hierarchyUp.y = hierarchyMove.y;
        if (shell.submitCommand(hierarchyUp).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot commit Hierarchy drag");
        const auto* sceneRoot =
            shell.sceneDocument().scene().findObject(shell.sceneDocument().scene().rootId());
        const auto& rootChildren = sceneRoot->childIds();
        const auto visualAAt =
            std::find(rootChildren.begin(), rootChildren.end(), hierarchyVisualAId);
        const auto* visualCObject = shell.sceneDocument().scene().findObject(hierarchyVisualC);
        if (visualAAt == rootChildren.end() || visualAAt == rootChildren.begin()
            || *(visualAAt - 1) != hierarchyVisualBId || !visualCObject
            || visualCObject->parentId() != shell.sceneDocument().scene().rootId())
            return fail("SCENE-HIERARCHY-SMOKE", "same-parent drop did not place A after B");

        const auto hierarchyAfterDrop = shell.sceneDocument().hierarchyEntries();
        const auto visualARow = std::find_if(
            hierarchyAfterDrop.begin(), hierarchyAfterDrop.end(), [&](const auto& entry) {
                return entry.objectId == hierarchyVisualAId;
            });
        const auto visualCRow = std::find_if(
            hierarchyAfterDrop.begin(), hierarchyAfterDrop.end(), [&](const auto& entry) {
                return entry.objectId == hierarchyVisualC;
            });
        if (visualARow == hierarchyAfterDrop.end() || visualCRow == hierarchyAfterDrop.end())
            return fail("SCENE-HIERARCHY-SMOKE", "reordered sibling rows are missing");
        EditorInputCommand highlightDown = hierarchyDown;
        highlightDown.y = layout::kSceneHierarchyTop
            + static_cast<float>(visualCRow - hierarchyAfterDrop.begin())
                * layout::kSceneHierarchyRowHeight
            + layout::kSceneHierarchyRowHeight * 0.5f;
        if (shell.submitCommand(highlightDown).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot start target highlight drag");
        EditorInputCommand highlightMove = hierarchyMove;
        highlightMove.y = layout::kSceneHierarchyTop
            + static_cast<float>(visualARow - hierarchyAfterDrop.begin())
                * layout::kSceneHierarchyRowHeight
            + layout::kSceneHierarchyRowHeight * 0.5f;
        highlightMove.deltaY = highlightMove.y - highlightDown.y;
        if (shell.submitCommand(highlightMove).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot position target highlight");
        auto highlightedDraw = shell.drawCommands();
        if (!highlightedDraw.ok())
            return fail("SCENE-HIERARCHY-SMOKE", "cannot draw active Hierarchy target");
        const float targetY = layout::kSceneHierarchyTop
            + static_cast<float>(visualARow - hierarchyAfterDrop.begin())
                * layout::kSceneHierarchyRowHeight;
        const auto hasTargetHighlight = std::any_of(highlightedDraw.commands().begin(),
            highlightedDraw.commands().end(),
            [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Box
                    && command.rect.y == targetY && command.color[0] == 64.0f / 255.0f
                    && command.color[1] == 83.0f / 255.0f && command.color[2] == 72.0f / 255.0f;
            });
        if (!hasTargetHighlight)
            return fail(
                "SCENE-HIERARCHY-SMOKE", "drag target highlight is absent from UI draw commands");
        EditorInputCommand highlightUp = highlightDown;
        highlightUp.kind = EditorInputKind::PointerUp;
        highlightUp.primaryButton = true;
        if (shell.submitCommand(highlightUp).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot finish the hierarchy drag");
        const auto expandedHierarchy = shell.sceneDocument().hierarchyEntries();
        const auto smokeParentRow = std::find_if(expandedHierarchy.begin(),
            expandedHierarchy.end(),
            [&](const auto& entry) { return entry.objectId == hierarchyParentId; });
        if (smokeParentRow == expandedHierarchy.end())
            return fail("SCENE-HIERARCHY-SMOKE", "hierarchy parent disappeared before folding");
        EditorInputCommand foldParent;
        foldParent.kind = EditorInputKind::PointerDown;
        foldParent.primaryButton = true;
        foldParent.button = EditorPointerButton::Left;
        foldParent.x = layout::kSceneHierarchyLeft + layout::kSceneHierarchyTextInset
            + static_cast<float>(smokeParentRow->depth) * layout::kSceneHierarchyDepthIndent - 4.5f;
        foldParent.y = layout::kSceneHierarchyTop
            + static_cast<float>(smokeParentRow - expandedHierarchy.begin())
                * layout::kSceneHierarchyRowHeight
            + layout::kSceneHierarchyRowHeight * 0.5f;
        if (shell.submitCommand(foldParent).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot collapse the hierarchy parent");
        foldParent.kind = EditorInputKind::PointerUp;
        if (shell.submitCommand(foldParent).status != EditorResponseStatus::Completed)
            return fail("SCENE-HIERARCHY-SMOKE", "cannot release the hierarchy disclosure");
        const auto foldedDraw = shell.drawCommands();
        if (!foldedDraw.ok())
            return fail("SCENE-HIERARCHY-SMOKE", "cannot draw the folded hierarchy");
        const bool childVisible = std::any_of(foldedDraw.commands().begin(),
            foldedDraw.commands().end(),
            [&](const auto& command) {
                return command.kind == renderer::ui::UiDrawCommandKind::Text
                    && command.text == "HierarchySmokeChild"
                    && command.rect.x < layout::kSceneHierarchyLeft + layout::kSceneHierarchyWidth
                    && command.rect.y < layout::kSceneHierarchyTop
                        + layout::kSceneHierarchyVisibleHeight
                    && command.rect.y + command.rect.height > layout::kSceneHierarchyTop;
            });
        if (childVisible || shell.sceneDocument().selectedObject() != hierarchyParentId)
            return fail("SCENE-HIERARCHY-SMOKE", "folding did not hide the child and select parent");
        std::printf(
            "PASS: ARP Editor Hierarchy sibling reorder, target highlight and node folding\n");
    }
    if (after) {
        const auto selected = shell.sceneDocument().selectedObject();
        if (!selected.valid()
            || shell.submitCommand(AddSceneComponentCommand("TimerComponent")).status
                != EditorResponseStatus::Completed)
            return fail("COMPONENT-AUTHORING-SMOKE", "cannot add Timer Inspector fixture");
        const auto* owner = shell.sceneDocument().scene().findObject(selected);
        const auto* timer = owner ? owner->getComponent<scene::TimerComponent>() : nullptr;
        if (!timer)
            return fail("COMPONENT-AUTHORING-SMOKE", "Timer Inspector fixture is absent");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("COMPONENT-AUTHORING-SMOKE", "cannot scroll to Timer properties");
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto* propertyView =
            layout.ok() ? layout.findWidget(shell.id("properties")) : nullptr;
        if (!propertyView)
            return fail("COMPONENT-AUTHORING-SMOKE", "cannot resolve Timer Inspector viewport");
        for (const auto* field : {"seconds", "repeat", "startOnEnable", "eventName"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(selected.value());
            widgetName.push_back('_');
            widgetName += std::to_string(timer->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout = widgetId.valid() ? layout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < propertyView->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > propertyView->rect.y + propertyView->rect.height)
                return fail("COMPONENT-AUTHORING-SMOKE",
                    "Timer authoring field is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor Timer authoring fields visible in Inspector\n");
        const auto* meshObject = shell.sceneDocument().scene().findObjectByName(
            core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()));
        if (!meshObject
            || shell.submitCommand(SelectSceneObjectCommand{meshObject->id().value()}).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(AddSceneComponentCommand("ParticleEmitter2DComponent")).status
                != EditorResponseStatus::Completed)
            return fail("PARTICLE-EMITTER-SMOKE", "cannot add Particle Emitter Inspector fixture");
        const auto* particleEmitter =
            meshObject->getComponent<scene2d::ParticleEmitter2DComponent>();
        if (!particleEmitter)
            return fail("PARTICLE-EMITTER-SMOKE", "Particle Emitter fixture is absent");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("PARTICLE-EMITTER-SMOKE", "cannot scroll to particle properties");
        const auto particleLayout = shell.runtime().canvas().resolveLayout();
        const auto* particleView =
            particleLayout.ok() ? particleLayout.findWidget(shell.id("properties")) : nullptr;
        if (!particleView)
            return fail("PARTICLE-EMITTER-SMOKE", "cannot resolve particle Inspector viewport");
        for (const auto* field : {"spriteId",
                 "lifetimeSeconds",
                 "size",
                 "velocitySpread_x",
                 "velocitySpread_y",
                 "color",
                 "seed",
                 "layer",
                 "order"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(meshObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(particleEmitter->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? particleLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < particleView->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > particleView->rect.y + particleView->rect.height)
                return fail("PARTICLE-EMITTER-SMOKE",
                    "Particle Emitter field is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor Particle Emitter authoring fields visible in Inspector\n");
        if (shell.submitCommand(AddSceneComponentCommand("SpriteTextComponent")).status
            != EditorResponseStatus::Completed)
            return fail("SPRITE-TEXT-SMOKE", "cannot add SpriteText Inspector fixture");
        const auto* spriteText = meshObject->getComponent<scene2d::SpriteTextComponent>();
        if (!spriteText)
            return fail("SPRITE-TEXT-SMOKE", "SpriteText Inspector fixture is absent");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("SPRITE-TEXT-SMOKE", "cannot scroll to SpriteText properties");
        const auto spriteTextLayout = shell.runtime().canvas().resolveLayout();
        const auto* spriteTextInspector =
            spriteTextLayout.ok() ? spriteTextLayout.findWidget(shell.id("properties")) : nullptr;
        if (!spriteTextInspector)
            return fail("SPRITE-TEXT-SMOKE", "cannot resolve SpriteText Inspector viewport");
        for (const auto* field : {"fontResourceId", "text", "tint", "layer"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(meshObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(spriteText->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? spriteTextLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < spriteTextInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > spriteTextInspector->rect.y + spriteTextInspector->rect.height)
                return fail("SPRITE-TEXT-SMOKE",
                    "SpriteText field is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor SpriteText authoring fields visible in Inspector\n");
        if (!meshObject
            || shell.submitCommand(SelectSceneObjectCommand{meshObject->id().value()}).status
                != EditorResponseStatus::Completed)
            return fail("COLOR-PICKER-SMOKE", "cannot restore the Mesh Inspector capture target");
        const auto colorProperties = shell.sceneDocument().inspectorProperties(meshObject->id());
        const auto colorProperty =
            std::find_if(colorProperties.begin(), colorProperties.end(), [](const auto& property) {
                return property.name == "baseColor"
                    && property.kind == SceneInspectorPropertyKind::Color4;
            });
        if (colorProperty == colorProperties.end())
            return fail("COLOR-PICKER-SMOKE", "Mesh color property disappeared after selection");
        core::pmr::String colorName("scene_property_", &core::defaultCpuMemoryResource());
        colorName += std::to_string(colorProperty->objectId);
        colorName.push_back('_');
        colorName += std::to_string(colorProperty->componentId);
        colorName += "_baseColor";
        ui::WidgetId colorWidgetId;
        for (const auto candidate : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(candidate);
            if (widget && widget->name() == colorName
                && widget->desc().controlType == ui::UiControlType::ColorPicker) {
                colorWidgetId = candidate;
                break;
            }
        }
        const auto colorLayout = shell.runtime().canvas().resolveLayout();
        const auto* colorWidgetLayout = colorLayout.ok() && colorWidgetId.valid()
            ? colorLayout.findWidget(colorWidgetId)
            : nullptr;
        const auto* inspectorLayout =
            colorLayout.ok() ? colorLayout.findWidget(shell.id("properties")) : nullptr;
        if (!colorWidgetLayout || !inspectorLayout || !colorWidgetLayout->visibleInHierarchy
            || colorWidgetLayout->rect.y < inspectorLayout->rect.y
            || colorWidgetLayout->rect.y + colorWidgetLayout->rect.height
                > inspectorLayout->rect.y + inspectorLayout->rect.height)
            return fail("COLOR-PICKER-SMOKE", "RGBA picker is not visible inside the Inspector");
        std::printf("PASS: ARP Editor Mesh RGBA ColorPicker visible in Inspector\n");

        if (shell.submitCommand(AddSceneComponentCommand("SpriteRendererComponent")).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(AddSceneComponentCommand("SpriteAnimationComponent")).status
                != EditorResponseStatus::Completed)
            return fail("SPRITE-ANIMATION-INSPECTOR-SMOKE",
                "cannot add SpriteRenderer/SpriteAnimation Inspector fixtures");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail(
                "SPRITE-ANIMATION-INSPECTOR-SMOKE", "cannot scroll to SpriteAnimation properties");
        const auto animationProperties =
            shell.sceneDocument().inspectorProperties(meshObject->id());
        const auto animationComponent = std::find_if(
            animationProperties.begin(), animationProperties.end(), [](const auto& property) {
                return property.componentType == "SpriteAnimationComponent";
            });
        if (animationComponent == animationProperties.end())
            return fail("SPRITE-ANIMATION-INSPECTOR-SMOKE",
                "SpriteAnimation Inspector component is absent");
        const auto animationLayout = shell.runtime().canvas().resolveLayout();
        const auto* animationInspector =
            animationLayout.ok() ? animationLayout.findWidget(shell.id("properties")) : nullptr;
        if (!animationInspector)
            return fail("SPRITE-ANIMATION-INSPECTOR-SMOKE",
                "cannot resolve SpriteAnimation Inspector viewport");
        for (const auto* field : {"animationResourceId", "speed", "paused", "eventBudget"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(meshObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(animationComponent->componentId);
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? animationLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < animationInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > animationInspector->rect.y + animationInspector->rect.height)
                return fail("SPRITE-ANIMATION-INSPECTOR-SMOKE",
                    "SpriteAnimation reflected field is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor SpriteAnimation reflected fields visible in Inspector\n");
        if (shell.submitCommand(AddSceneComponentCommand("AnimatorComponent")).status
            != EditorResponseStatus::Completed)
            return fail("ANIMATOR-INSPECTOR-SMOKE", "cannot add Animator Inspector fixture");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("ANIMATOR-INSPECTOR-SMOKE", "cannot scroll to Animator properties");
        const auto animatorProperties = shell.sceneDocument().inspectorProperties(meshObject->id());
        const auto animatorProperty = std::find_if(
            animatorProperties.begin(), animatorProperties.end(), [](const auto& property) {
                return property.componentType == "AnimatorComponent";
            });
        const auto animatorLayout = shell.runtime().canvas().resolveLayout();
        const auto* animatorInspector =
            animatorLayout.ok() ? animatorLayout.findWidget(shell.id("properties")) : nullptr;
        if (animatorProperty == animatorProperties.end() || !animatorInspector)
            return fail(
                "ANIMATOR-INSPECTOR-SMOKE", "Animator Inspector component or viewport is absent");
        for (const auto* field : {"prefabResourceId", "defaultStateName"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(meshObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(animatorProperty->componentId);
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? animatorLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < animatorInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > animatorInspector->rect.y + animatorInspector->rect.height)
                return fail("ANIMATOR-INSPECTOR-SMOKE",
                    "Animator reflected field is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor Animator reflected fields visible in Inspector\n");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, -450.0f))
                .ok())
            return fail("PARTICLE-EMITTER-SMOKE", "cannot position particle fields for capture");

        physics2d::WorldConfig rigidBodySmokeConfig{
            {0.0f, -9.8f}, 32, 64, 16, 128, 256, 128, 32, 4, 1.0f / 60.0f};
        auto rigidBodySmokeWorld =
            core::makeShared<physics2d::PhysicsWorld2D>(rigidBodySmokeConfig);
        if (!rigidBodySmokeWorld->valid())
            return fail(
                "RIGIDBODY-INSPECTOR-SMOKE", "cannot create bounded PhysicsWorld2D fixture");
        shell.sceneDocument().setPhysicsWorld2DProvider([rigidBodySmokeWorld] {
            return rigidBodySmokeWorld;
        });
        if (shell.submitCommand(SelectSceneObjectCommand{meshObject->id().value()}).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(AddSceneComponentCommand("RigidBody2DComponent")).status
                != EditorResponseStatus::Completed)
            return fail("RIGIDBODY-INSPECTOR-SMOKE", "cannot add RigidBody2D Inspector fixture");
        const auto* rigidBody = meshObject->getComponent<physics2d::RigidBody2DComponent>();
        if (!rigidBody || !rigidBody->handle())
            return fail("RIGIDBODY-INSPECTOR-SMOKE", "live rigid body fixture is absent");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("RIGIDBODY-INSPECTOR-SMOKE", "cannot scroll to rigid body properties");
        const auto rigidBodyLayout = shell.runtime().canvas().resolveLayout();
        const auto* rigidBodyInspector =
            rigidBodyLayout.ok() ? rigidBodyLayout.findWidget(shell.id("properties")) : nullptr;
        if (!rigidBodyInspector)
            return fail(
                "RIGIDBODY-INSPECTOR-SMOKE", "cannot resolve rigid body Inspector viewport");
        for (const auto* field : {"type",
                 "velocity_x",
                 "velocity_y",
                 "angularVelocity",
                 "gravityScale",
                 "linearDamping",
                 "angularDamping",
                 "bullet",
                 "fixedRotation"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(meshObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(rigidBody->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? rigidBodyLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < rigidBodyInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > rigidBodyInspector->rect.y + rigidBodyInspector->rect.height)
                return fail("RIGIDBODY-INSPECTOR-SMOKE",
                    "RigidBody2D authoring field is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor RigidBody2D authoring fields visible in Inspector\n");
        if (shell.submitCommand(AddSceneComponentCommand("Collider2DComponent")).status
            != EditorResponseStatus::Completed)
            return fail("COLLIDER2D-INSPECTOR-SMOKE", "cannot add Collider2D Inspector fixture");
        const auto* collider = meshObject->getComponent<physics2d::Collider2DComponent>();
        if (!collider || !collider->handle())
            return fail("COLLIDER2D-INSPECTOR-SMOKE", "live Collider2D fixture is absent");
        SetSceneComponentPropertyCommand colliderPoints("points");
        colliderPoints.objectId = meshObject->id().value();
        colliderPoints.componentId = collider->id().value();
        colliderPoints.valueKind = ScenePropertyValueKind::Vector2List;
        colliderPoints.vector2ListValue = {{-1.0f, -0.5f}, {1.0f, -0.5f}, {0.0f, 1.0f}};
        if (shell.submitCommand(std::move(colliderPoints)).status
            != EditorResponseStatus::Completed)
            return fail("COLLIDER2D-INSPECTOR-SMOKE", "cannot write point-list fixture");
        const auto rootObjectId = shell.sceneDocument().scene().objects().front()->id().value();
        shell.selectSceneObject(rootObjectId);
        shell.selectSceneObject(meshObject->id().value());
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("COLLIDER2D-INSPECTOR-SMOKE", "cannot scroll to collider properties");
        const auto colliderLayout = shell.runtime().canvas().resolveLayout();
        const auto* colliderInspector =
            colliderLayout.ok() ? colliderLayout.findWidget(shell.id("properties")) : nullptr;
        if (!colliderInspector)
            return fail("COLLIDER2D-INSPECTOR-SMOKE", "cannot resolve collider Inspector viewport");
        for (const auto* field : {"points_point_add", "points_point_2_x", "points_point_2_y"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(meshObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(collider->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? colliderLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < colliderInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > colliderInspector->rect.y + colliderInspector->rect.height) {
                return fail("COLLIDER2D-INSPECTOR-SMOKE",
                    "Collider2D authoring field is absent or outside the Inspector viewport");
            }
        }
        core::pmr::String addPointSource("scene_property_", &core::defaultCpuMemoryResource());
        addPointSource += std::to_string(meshObject->id().value());
        addPointSource.push_back('_');
        addPointSource += std::to_string(collider->id().value());
        addPointSource += "_points_point_add";
        shell.activate(addPointSource);
        if (collider->desc().points.size() != 4u)
            return fail("COLLIDER2D-INSPECTOR-SMOKE", "Add Point did not append one point");
        core::pmr::String removePointSource("scene_property_", &core::defaultCpuMemoryResource());
        removePointSource += std::to_string(meshObject->id().value());
        removePointSource.push_back('_');
        removePointSource += std::to_string(collider->id().value());
        removePointSource += "_points_point_remove_3";
        shell.activate(removePointSource);
        if (collider->desc().points.size() != 3u)
            return fail(
                "COLLIDER2D-INSPECTOR-SMOKE", "Remove Point did not remove the selected point");
        SetSceneComponentPropertyCommand colliderType("type");
        colliderType.objectId = meshObject->id().value();
        colliderType.componentId = collider->id().value();
        colliderType.valueKind = ScenePropertyValueKind::Enumeration;
        colliderType.integerValue = static_cast<int>(physics2d::ShapeType::Convex);
        if (shell.submitCommand(std::move(colliderType)).status != EditorResponseStatus::Completed)
            return fail("COLLIDER2D-INSPECTOR-SMOKE", "cannot activate convex point-list shape");
        std::printf("PASS: ARP Editor Collider2D authoring fields visible in Inspector\n");

        if (!tests::loadSpriteFixture(shell.projectResources())
            || !tests::loadTileFixture(shell.projectResources()))
            return fail("TILEMAP-INSPECTOR-SMOKE", "cannot load deterministic TileMap resources");
        if (shell.submitCommand(CreateSceneObjectCommand("TileMap Inspector")).status
            != EditorResponseStatus::Completed)
            return fail("TILEMAP-INSPECTOR-SMOKE", "cannot create TileMap Inspector target");
        tileMapCaptureObject = shell.sceneDocument().selectedObject();
        AddSceneComponentCommand tileMapAdd("TileMapResourceComponent");
        tileMapAdd.objectId = tileMapCaptureObject.value();
        SetSceneComponentPropertyCommand tileMapResource("tileMapResourceId");
        tileMapResource.valueKind = ScenePropertyValueKind::ResourceReference;
        tileMapResource.stringValue = tests::tileMapId;
        tileMapAdd.initialProperties.push_back(std::move(tileMapResource));
        SetSceneComponentPropertyCommand tileMapResidentBudget("maxResidentChunks");
        tileMapResidentBudget.valueKind = ScenePropertyValueKind::UnsignedInteger;
        tileMapResidentBudget.unsignedIntegerValue = 2u;
        tileMapAdd.initialProperties.push_back(std::move(tileMapResidentBudget));
        SetSceneComponentPropertyCommand tileMapVisibleBudget("maxVisibleCells");
        tileMapVisibleBudget.valueKind = ScenePropertyValueKind::UnsignedInteger;
        tileMapVisibleBudget.unsignedIntegerValue = 12u;
        tileMapAdd.initialProperties.push_back(std::move(tileMapVisibleBudget));
        SetSceneComponentPropertyCommand tileMapObjectId("firstObjectId");
        tileMapObjectId.valueKind = ScenePropertyValueKind::UnsignedInteger;
        tileMapObjectId.unsignedIntegerValue = 100u;
        tileMapAdd.initialProperties.push_back(std::move(tileMapObjectId));
        if (shell.submitCommand(std::move(tileMapAdd)).status != EditorResponseStatus::Completed)
            return fail("TILEMAP-INSPECTOR-SMOKE", "cannot create configured TileMap component");
        const auto* tileMapObject = shell.sceneDocument().scene().findObject(tileMapCaptureObject);
        const auto* tileMap = tileMapObject
            ? tileMapObject->getComponent<scene2d::TileMapResourceComponent>()
            : nullptr;
        if (!tileMap || !tileMap->runtime())
            return fail("TILEMAP-INSPECTOR-SMOKE", "live TileMap runtime fixture is absent");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("TILEMAP-INSPECTOR-SMOKE", "cannot scroll to TileMap properties");
        const auto tileMapLayout = shell.runtime().canvas().resolveLayout();
        const auto* tileMapInspector =
            tileMapLayout.ok() ? tileMapLayout.findWidget(shell.id("properties")) : nullptr;
        if (!tileMapInspector)
            return fail("TILEMAP-INSPECTOR-SMOKE", "cannot resolve TileMap Inspector viewport");
        for (const auto* field :
            {"tileMapResourceId", "maxResidentChunks", "maxVisibleCells", "firstObjectId"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(tileMapCaptureObject.value());
            widgetName.push_back('_');
            widgetName += std::to_string(tileMap->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? tileMapLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < tileMapInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > tileMapInspector->rect.y + tileMapInspector->rect.height)
                return fail("TILEMAP-INSPECTOR-SMOKE",
                    "TileMap authoring field is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor TileMap resource and budget fields visible in Inspector\n");

        if (shell.submitCommand(CreateSceneObjectCommand("Joint Target")).status
            != EditorResponseStatus::Completed)
            return fail("JOINT2D-INSPECTOR-SMOKE", "cannot create joint target object");
        const auto jointTargetId = shell.sceneDocument().selectedObject();
        if (shell.submitCommand(AddSceneComponentCommand("RigidBody2DComponent")).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(SelectSceneObjectCommand{meshObject->id().value()}).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(AddSceneComponentCommand("Joint2DComponent")).status
                != EditorResponseStatus::Completed)
            return fail("JOINT2D-INSPECTOR-SMOKE", "cannot create stable-reference joint fixture");
        const auto* jointOwner = shell.sceneDocument().scene().findObject(meshObject->id());
        const auto* joint =
            jointOwner ? jointOwner->getComponent<physics2d::Joint2DComponent>() : nullptr;
        if (!jointTargetId.valid() || !joint)
            return fail("JOINT2D-INSPECTOR-SMOKE", "joint fixture is absent");
        SetSceneComponentPropertyCommand jointBodyA("bodyA");
        jointBodyA.objectId = meshObject->id().value();
        jointBodyA.componentId = joint->id().value();
        jointBodyA.valueKind = ScenePropertyValueKind::SceneObjectReference;
        jointBodyA.sceneObjectReferenceValue = meshObject->id().value();
        SetSceneComponentPropertyCommand jointBodyB("bodyB");
        jointBodyB.objectId = meshObject->id().value();
        jointBodyB.componentId = joint->id().value();
        jointBodyB.valueKind = ScenePropertyValueKind::SceneObjectReference;
        jointBodyB.sceneObjectReferenceValue = jointTargetId.value();
        if (shell.submitCommand(std::move(jointBodyA)).status != EditorResponseStatus::Completed
            || shell.submitCommand(std::move(jointBodyB)).status != EditorResponseStatus::Completed
            || !joint->handle() || joint->bodyAObjectId() != meshObject->id().value()
            || joint->bodyBObjectId() != jointTargetId.value())
            return fail("JOINT2D-INSPECTOR-SMOKE", "cannot bind live joint endpoints");
        if (shell.submitCommand(SelectSceneObjectCommand{jointTargetId.value()}).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(SelectSceneObjectCommand{meshObject->id().value()}).status
                != EditorResponseStatus::Completed)
            return fail("JOINT2D-INSPECTOR-SMOKE", "cannot refresh bound reference values");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("JOINT2D-INSPECTOR-SMOKE", "cannot scroll to joint properties");
        const auto jointLayout = shell.runtime().canvas().resolveLayout();
        const auto* jointInspector =
            jointLayout.ok() ? jointLayout.findWidget(shell.id("properties")) : nullptr;
        if (!jointInspector)
            return fail("JOINT2D-INSPECTOR-SMOKE", "cannot resolve joint Inspector viewport");
        for (const auto* field : {"bodyA", "bodyB"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(meshObject->id().value());
            widgetName.push_back('_');
            widgetName += std::to_string(joint->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout = widgetId.valid() ? jointLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < jointInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > jointInspector->rect.y + jointInspector->rect.height)
                return fail("JOINT2D-INSPECTOR-SMOKE",
                    "Joint2D reference picker is absent or outside the Inspector viewport");
        }
        std::printf("PASS: ARP Editor Joint2D stable-reference fields visible in Inspector\n");
    }
    if (viewportPipelineSmoke || viewportRuntimeSmoke) {
        shell.activate("workspace_3D");
        if (viewportPipelineSmoke)
            shell.activate("tool_2");
    }
    if (after && tileMapCaptureObject.valid()) {
        if (shell.submitCommand(SelectSceneObjectCommand{tileMapCaptureObject.value()}).status
                != EditorResponseStatus::Completed
            || !shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("TILEMAP-INSPECTOR-SMOKE", "cannot restore TileMap capture state");
        const auto* tileMapObject = shell.sceneDocument().scene().findObject(tileMapCaptureObject);
        const auto* tileMap = tileMapObject
            ? tileMapObject->getComponent<scene2d::TileMapResourceComponent>()
            : nullptr;
        if (!tileMap || !tileMap->runtime())
            return fail(
                "TILEMAP-INSPECTOR-SMOKE", "TileMap runtime was lost before final presentation");
        const auto properties = shell.sceneDocument().inspectorProperties(tileMapCaptureObject);
        const auto resourceProperty =
            std::find_if(properties.begin(), properties.end(), [&](const auto& property) {
                return property.componentId == tileMap->id().value()
                    && property.name == "tileMapResourceId";
            });
        const auto references = shell.projectResources().snapshotReferences("TileMapResource", 32u);
        const bool readyReference = references.success
            && std::any_of(
                references.entries.begin(), references.entries.end(), [&](const auto& entry) {
                    return entry.state == resource::ResourceState::Ready
                        && entry.reference.id == tests::tileMapId;
                });
        if (resourceProperty == properties.end()
            || resourceProperty->resourceType != "TileMapResource" || !readyReference)
            return fail("TILEMAP-INSPECTOR-SMOKE",
                "TileMap resource is not Ready in the Inspector's exact-type provider");
        const auto inspectorQuery =
            shell.submitCommand(GetSceneInspectorQuery{tileMapCaptureObject.value()});
        if (inspectorQuery.status != EditorResponseStatus::Completed
            || !inspectorQuery.hasSceneInspector)
            return fail("TILEMAP-INSPECTOR-SMOKE", "cannot query final TileMap Inspector state");
        const auto presentedResource =
            std::find_if(inspectorQuery.sceneInspector.properties.begin(),
                inspectorQuery.sceneInspector.properties.end(),
                [&](const auto& property) {
                    return property.componentId == tileMap->id().value()
                        && property.name == "tileMapResourceId";
                });
        if (presentedResource == inspectorQuery.sceneInspector.properties.end()
            || !presentedResource->resourceDiagnostic.empty()
            || std::none_of(presentedResource->enumOptions.begin(),
                presentedResource->enumOptions.end(),
                [&](const auto& option) {
                    return option.commandValue == tests::tileMapId;
                })) {
            return fail("TILEMAP-INSPECTOR-SMOKE",
                "TileMap Inspector cannot resolve the Ready resource option");
        }

        const auto* meshObject = shell.sceneDocument().scene().findObjectByName(
            core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()));
        if (!meshObject
            || shell.submitCommand(CreateSceneObjectCommand("Object Links Inspector")).status
                != EditorResponseStatus::Completed)
            return fail(
                "OBJECT-LINKS-INSPECTOR-SMOKE", "cannot create Object Links fixture object");
        const auto linksCaptureObject = shell.sceneDocument().selectedObject();
        if (shell.submitCommand(AddSceneComponentCommand("ObjectLinksComponent")).status
            != EditorResponseStatus::Completed)
            return fail("OBJECT-LINKS-INSPECTOR-SMOKE", "cannot add Object Links component");
        auto* linksObject = shell.sceneDocument().scene().findObject(linksCaptureObject);
        auto* objectLinks =
            linksObject ? linksObject->getComponent<scene::ObjectLinksComponent>() : nullptr;
        if (!objectLinks)
            return fail("OBJECT-LINKS-INSPECTOR-SMOKE", "Object Links component is absent");
        SetSceneComponentPropertyCommand setObjectLinks("links");
        setObjectLinks.objectId = linksCaptureObject.value();
        setObjectLinks.componentId = objectLinks->id().value();
        setObjectLinks.valueKind = ScenePropertyValueKind::SceneObjectLinkList;
        setObjectLinks.sceneObjectLinkListValue.emplace_back();
        setObjectLinks.sceneObjectLinkListValue.back().name = "meshTarget";
        setObjectLinks.sceneObjectLinkListValue.back().targetObjectId = meshObject->id().value();
        if (shell.submitCommand(std::move(setObjectLinks)).status
            != EditorResponseStatus::Completed)
            return fail("OBJECT-LINKS-INSPECTOR-SMOKE", "cannot author a scene object link");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("OBJECT-LINKS-INSPECTOR-SMOKE", "cannot scroll to object link fields");
        const auto objectLinksLayout = shell.runtime().canvas().resolveLayout();
        const auto* objectLinksInspector =
            objectLinksLayout.ok() ? objectLinksLayout.findWidget(shell.id("properties")) : nullptr;
        if (!objectLinksInspector)
            return fail("OBJECT-LINKS-INSPECTOR-SMOKE", "cannot resolve Inspector viewport");
        for (const auto* field :
            {"links_link_name_0", "links_link_target_0", "links_link_remove_0", "links_link_add"}) {
            core::pmr::String widgetName("scene_property_", &core::defaultCpuMemoryResource());
            widgetName += std::to_string(linksCaptureObject.value());
            widgetName.push_back('_');
            widgetName += std::to_string(objectLinks->id().value());
            widgetName.push_back('_');
            widgetName += field;
            ui::WidgetId widgetId;
            for (const auto candidate : shell.runtime().canvas().traversal()) {
                const auto* widget = shell.runtime().canvas().findWidget(candidate);
                if (widget && widget->name() == widgetName) {
                    widgetId = candidate;
                    break;
                }
            }
            const auto* fieldLayout =
                widgetId.valid() ? objectLinksLayout.findWidget(widgetId) : nullptr;
            if (!fieldLayout || !fieldLayout->visibleInHierarchy
                || fieldLayout->rect.y < objectLinksInspector->rect.y
                || fieldLayout->rect.y + fieldLayout->rect.height
                    > objectLinksInspector->rect.y + objectLinksInspector->rect.height)
                return fail("OBJECT-LINKS-INSPECTOR-SMOKE", field);
        }
        core::pmr::String addLinkSource("scene_property_", &core::defaultCpuMemoryResource());
        addLinkSource += std::to_string(linksCaptureObject.value());
        addLinkSource.push_back('_');
        addLinkSource += std::to_string(objectLinks->id().value());
        addLinkSource += "_links_link_add";
        shell.activate(addLinkSource);
        if (objectLinks->links.size() != 2u)
            return fail("OBJECT-LINKS-INSPECTOR-SMOKE", "Add Link did not append one entry");
        core::pmr::String removeLinkSource("scene_property_", &core::defaultCpuMemoryResource());
        removeLinkSource += std::to_string(linksCaptureObject.value());
        removeLinkSource.push_back('_');
        removeLinkSource += std::to_string(objectLinks->id().value());
        removeLinkSource += "_links_link_remove_1";
        shell.activate(removeLinkSource);
        if (objectLinks->links.size() != 1u || objectLinks->links.front().name != "meshTarget")
            return fail(
                "OBJECT-LINKS-INSPECTOR-SMOKE", "Remove Link did not restore the authored entry");
        std::printf("PASS: ARP Editor Object Links reflected list controls and commands\n");

        auto& temporal = linksObject->addComponent<scene::CameraTemporalComponent>();
        temporal.set_previous_projection(68.0f, 1.5f, 0.2f, 150.0f);
        temporal.setPreviousPose({1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f});
        temporal.setPoseValid(true);
        if (shell.submitCommand(SelectSceneObjectCommand{meshObject->id().value()}).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(SelectSceneObjectCommand{linksCaptureObject.value()}).status
                != EditorResponseStatus::Completed)
            return fail("RUNTIME-INSPECTOR-SMOKE", "cannot refresh runtime Inspector fixture");
        if (!shell.runtime()
                .interact(ui::UiControlInteraction::scrollBy(shell.id("properties"), 0.0f, 5000.0f))
                .ok())
            return fail("RUNTIME-INSPECTOR-SMOKE", "cannot scroll to runtime properties");

        core::pmr::String runtimeFieldName("scene_property_", &core::defaultCpuMemoryResource());
        runtimeFieldName += std::to_string(linksCaptureObject.value());
        runtimeFieldName.push_back('_');
        runtimeFieldName += std::to_string(temporal.id().value());
        runtimeFieldName += "_previousFovDegrees_";
        ui::WidgetId runtimeFieldId;
        for (const auto candidate : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(candidate);
            if (widget && widget->name() == runtimeFieldName) {
                runtimeFieldId = candidate;
                if (widget->desc().text.find("68") == core::pmr::String::npos
                    || widget->desc().text.find("read only") == core::pmr::String::npos)
                    return fail("RUNTIME-INSPECTOR-SMOKE",
                        "runtime field does not show its current value as read-only");
                break;
            }
        }
        const auto runtimeLayout = shell.runtime().canvas().resolveLayout();
        const auto* runtimeInspector =
            runtimeLayout.ok() ? runtimeLayout.findWidget(shell.id("properties")) : nullptr;
        const auto* runtimeFieldLayout =
            runtimeFieldId.valid() ? runtimeLayout.findWidget(runtimeFieldId) : nullptr;
        if (!runtimeFieldLayout || !runtimeInspector || !runtimeFieldLayout->visibleInHierarchy
            || runtimeFieldLayout->rect.y < runtimeInspector->rect.y
            || runtimeFieldLayout->rect.y + runtimeFieldLayout->rect.height
                > runtimeInspector->rect.y + runtimeInspector->rect.height)
            return fail("RUNTIME-INSPECTOR-SMOKE",
                "runtime field is absent or outside the Inspector viewport");
        std::printf(
            "PASS: ARP Editor runtime reflection values visible as read-only Inspector rows\n");
    }
    if (after) {
        terrainGizmoFixtureDirectory.path = std::filesystem::temp_directory_path()
            / ("arp-editor-after-terrain-gizmo-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(terrainGizmoFixtureDirectory.path);
        if (!tests::loadTerrainGizmoFixture(
                shell.projectResources(), terrainGizmoFixtureDirectory.path))
            return fail("TERRAIN-GIZMO-SMOKE", "cannot load the authored TerrainAsset fixture");
        if (shell.submitCommand(CreateSceneObjectCommand("Terrain Gizmo Smoke")).status
            != EditorResponseStatus::Completed)
            return fail("TERRAIN-GIZMO-SMOKE", "cannot create the Terrain GameObject");
        const auto terrainObjectId = shell.sceneDocument().selectedObject();
        AddSceneComponentCommand addTerrain("TerrainComponent");
        addTerrain.objectId = terrainObjectId.value();
        SetSceneComponentPropertyCommand terrainResourceCommand("resourceId");
        terrainResourceCommand.valueKind = ScenePropertyValueKind::ResourceReference;
        terrainResourceCommand.stringValue = tests::terrainGizmoId;
        addTerrain.initialProperties.push_back(std::move(terrainResourceCommand));
        if (shell.submitCommand(std::move(addTerrain)).status != EditorResponseStatus::Completed)
            return fail("TERRAIN-GIZMO-SMOKE", "cannot bind TerrainComponent to its asset");
        AddSceneComponentCommand addTerrainAuthoring("TerrainAuthoringComponent");
        addTerrainAuthoring.objectId = terrainObjectId.value();
        SetSceneComponentPropertyCommand terrainExtentCommand("halfExtent");
        terrainExtentCommand.valueKind = ScenePropertyValueKind::Number;
        terrainExtentCommand.numberValue = 3.0f;
        addTerrainAuthoring.initialProperties.push_back(std::move(terrainExtentCommand));
        if (shell.submitCommand(std::move(addTerrainAuthoring)).status
            != EditorResponseStatus::Completed)
            return fail("TERRAIN-GIZMO-SMOKE", "cannot configure TerrainAuthoringComponent");
        AddSceneComponentCommand addCritterField("CritterFieldComponent");
        addCritterField.objectId = terrainObjectId.value();
        SetSceneComponentPropertyCommand critterMaxXCommand("worldMaxX");
        critterMaxXCommand.valueKind = ScenePropertyValueKind::Number;
        critterMaxXCommand.numberValue = 1.5f;
        addCritterField.initialProperties.push_back(std::move(critterMaxXCommand));
        SetSceneComponentPropertyCommand critterMinZCommand("worldMinZ");
        critterMinZCommand.valueKind = ScenePropertyValueKind::Number;
        critterMinZCommand.numberValue = -4.5f;
        addCritterField.initialProperties.push_back(std::move(critterMinZCommand));
        SetSceneComponentPropertyCommand critterMaxZCommand("worldMaxZ");
        critterMaxZCommand.valueKind = ScenePropertyValueKind::Number;
        critterMaxZCommand.numberValue = -1.5f;
        addCritterField.initialProperties.push_back(std::move(critterMaxZCommand));
        SetSceneComponentPropertyCommand critterWaterLevelCommand("waterLevel");
        critterWaterLevelCommand.valueKind = ScenePropertyValueKind::Number;
        critterWaterLevelCommand.numberValue = -0.3f;
        addCritterField.initialProperties.push_back(std::move(critterWaterLevelCommand));
        if (shell.submitCommand(std::move(addCritterField)).status
            != EditorResponseStatus::Completed)
            return fail("CRITTER-FIELD-GIZMO-SMOKE", "cannot configure CritterFieldComponent");
        SetSceneObjectTransformCommand terrainTransform;
        terrainTransform.objectId = terrainObjectId.value();
        terrainTransform.translation = {0.0f, -0.4f, -3.0f};
        terrainTransform.scale = {0.4f, 0.4f, 0.4f};
        if (shell.submitCommand(terrainTransform).status != EditorResponseStatus::Completed)
            return fail("TERRAIN-GIZMO-SMOKE", "cannot place the Terrain preview");
        const auto* terrainObject = shell.sceneDocument().scene().findObject(terrainObjectId);
        const auto* terrain =
            terrainObject ? terrainObject->getComponent<scene3d::TerrainComponent>() : nullptr;
        const auto* terrainAuthoring = terrainObject
            ? terrainObject->getComponent<scene3d::TerrainAuthoringComponent>()
            : nullptr;
        const auto* critterField =
            terrainObject ? terrainObject->getComponent<scene3d::CritterFieldComponent>() : nullptr;
        const auto terrainHandle =
            shell.projectResources().find<resource::TerrainAssetResource>(tests::terrainGizmoId);
        if (terrain == nullptr || !terrainHandle.ready())
            return fail("TERRAIN-GIZMO-SMOKE", "Terrain authoring/resource owner is not Ready");
        const auto gizmos = editor::buildEditorSceneComponentGizmos(shell.sceneDocument().scene(),
            shell.projectResources(),
            1440.0f / 810.0f,
            editor::kEditorSceneComponentGizmoSegmentBudget);
        const auto terrainSegments =
            std::count_if(gizmos.segments.begin(), gizmos.segments.end(), [&](const auto& segment) {
                return segment.objectId == terrainObjectId && segment.componentId == terrain->id()
                    && segment.kind == editor::EditorSceneComponentGizmoKind::TerrainBounds;
            });
        const auto terrainAuthoringSegments =
            std::count_if(gizmos.segments.begin(), gizmos.segments.end(), [&](const auto& segment) {
                return segment.objectId == terrainObjectId && terrainAuthoring != nullptr
                    && segment.componentId == terrainAuthoring->id()
                    && segment.kind == editor::EditorSceneComponentGizmoKind::TerrainAuthoring;
            });
        const auto critterFieldSegments =
            std::count_if(gizmos.segments.begin(), gizmos.segments.end(), [&](const auto& segment) {
                return segment.objectId == terrainObjectId && critterField != nullptr
                    && segment.componentId == critterField->id()
                    && segment.kind == editor::EditorSceneComponentGizmoKind::CritterField;
            });
        if (!gizmos.valid || terrainSegments != 12u || terrainAuthoringSegments != 4u
            || critterFieldSegments != 4u)
            return fail("TERRAIN-GIZMO-SMOKE",
                "Terrain or CritterField authoring bounds failed to reach the viewport overlay");
        std::printf(
            "PASS: resolved TerrainAsset, authoring extent and CritterField bounds reach the viewport gizmo overlay\n");
    }
    auto authored = shell.drawCommands();
    if (!authored.ok())
        return fail("DRAW", authored.diagnostics());
    auto assets = EditorUiAssets::build(shell, root, authored.commands());
    if (!assets.ok())
        return fail(assets.stage(), assets.diagnostic());
    auto& atlas = assets.atlas();
    platform::WindowSystem platform;
    auto initialized = platform.initialize();
    if (!initialized.success)
        return fail("PLATFORM", initialized.diagnostics);
    auto window = platform.createWindow({"ARP Editor", 1440, 810, hidden, true});
    if (!window.valid())
        return fail("WINDOW", "creation failed");
    GamePreviewHost gamePreview;
    if (!hidden && !gamePreview.hasEditorWindow())
        return fail("GAME-PREVIEW", "cannot locate the editor native window");
    if (hover) {
        input::InputFrame frame;
        frame.add(input::InputEvent::pointerMoved(100.0f, 415.0f));
        auto routed = shell.runtime().dispatchFrame(frame, {window.id(), 512});
        if (!routed.ok())
            return fail("HOVER", routed.diagnostic.message);
    }
    auto system = platform::createDefaultRenderSystem(platform, window, "ArpEditor");
    if (!system.valid())
        return fail("RENDER-SYSTEM", system.diagnostics());
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
    RENDERDOC_API_1_4_0* capture = nullptr;
    if (auto module = GetModuleHandleA("renderdoc.dll")) {
        auto get = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
        if (get)
            get(eRENDERDOC_API_Version_1_4_0, reinterpret_cast<void**>(&capture));
    }
    if (capture) {
        if (auto path = std::getenv("ARP_RENDERDOC_CAPTURE_PATH"))
            capture->SetCaptureFilePathTemplate(path);
        capture->MaskOverlayBits(0, 0);
    }
#endif
    editor::EditorSceneViewportSession viewportSession(
        editor::kEditorSceneViewportInteractiveSessionConfig);
    const auto resolveViewportExtent = [&]() -> rhi::Extent2D {
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto* viewport = layout.ok() ? layout.findWidget(shell.id("viewport")) : nullptr;
        if (!viewport || viewport->rect.width <= 0.0f || viewport->rect.height <= 0.0f)
            return {};
        return {static_cast<std::uint32_t>(
                    std::lround(viewport->rect.width * window.pixelWidth() / 1920.0f)),
            static_cast<std::uint32_t>(
                std::lround(viewport->rect.height * window.pixelHeight() / 1080.0f))};
    };
    const auto shaderRoot = std::filesystem::path(ARP_SHADER_DIR);
    const auto viewportSourcePath = (shaderRoot / "editor_viewport_pbr.slang").generic_string();
    const auto viewportSpirvPath = (shaderRoot / "editor_viewport_pbr.spv").generic_string();
    const auto viewportMslPath = (shaderRoot / "editor_viewport_pbr.metal").generic_string();
    const auto viewportWgslPath = (shaderRoot / "editor_viewport_pbr.wgsl").generic_string();
    const auto viewportReflectionPath =
        (shaderRoot / "editor_viewport_pbr.reflection.yaml").generic_string();
    const editor::EditorSceneViewportShaderArtifacts viewportArtifacts{viewportSourcePath,
        viewportSpirvPath,
        viewportMslPath,
        viewportWgslPath,
        viewportReflectionPath};
    const auto spriteSourcePath = (shaderRoot / "editor_viewport_sprite.slang").generic_string();
    const auto spriteSpirvPath = (shaderRoot / "editor_viewport_sprite.spv").generic_string();
    const auto spriteMslPath = (shaderRoot / "editor_viewport_sprite.metal").generic_string();
    const auto spriteWgslPath = (shaderRoot / "editor_viewport_sprite.wgsl").generic_string();
    const auto spriteReflectionPath =
        (shaderRoot / "editor_viewport_sprite.reflection.yaml").generic_string();
    const editor::EditorSceneViewportShaderArtifacts viewportSpriteArtifacts{
        spriteSourcePath, spriteSpirvPath, spriteMslPath, spriteWgslPath, spriteReflectionPath};
    if (viewportPipelineSmoke || viewportRuntimeSmoke) {
        core::pmr::String resourceDiagnostics(&core::defaultCpuMemoryResource());
        if (!installViewportSmokeSceneResources(shell, resourceDiagnostics))
            return fail("VIEWPORT-RESOURCES", resourceDiagnostics);
        auto* sceneObject = shell.sceneDocument().scene().findObjectByName(
            core::pmr::String("MeshInstance", &core::defaultCpuMemoryResource()));
        auto* meshComponent =
            sceneObject ? sceneObject->getComponent<scene3d::MeshComponent>() : nullptr;
        if (!sceneObject || !meshComponent)
            return fail("VIEWPORT-SCENE", "default editor scene is missing its MeshComponent");
        const auto assignResource = [&](const char* property, const char* resourceId) {
            editor::SetSceneComponentPropertyCommand command(property);
            command.objectId = sceneObject->id().value();
            command.componentId = meshComponent->id().value();
            command.valueKind = editor::ScenePropertyValueKind::ResourceReference;
            command.stringValue = resourceId;
            return shell.submitCommand(std::move(command)).status
                == editor::EditorResponseStatus::Completed;
        };
        if (!assignResource("resourceId", kViewportSmokeMeshId)
            || !assignResource("materialResourceId", kViewportSmokeMaterialId))
            return fail("VIEWPORT-SCENE", "resource assignment command was rejected");
        if (shell.submitCommand(editor::CreateSceneObjectCommand("MeshInstanceSecondary")).status
            != editor::EditorResponseStatus::Completed)
            return fail("VIEWPORT-SCENE", "cannot create secondary viewport smoke object");
        auto* secondaryObject = shell.sceneDocument().scene().findObjectByName(
            core::pmr::String("MeshInstanceSecondary", &core::defaultCpuMemoryResource()));
        if (!secondaryObject
            || shell.submitCommand(editor::AddSceneComponentCommand("MeshComponent")).status
                != editor::EditorResponseStatus::Completed)
            return fail("VIEWPORT-SCENE", "cannot add secondary MeshComponent");
        auto* secondaryMesh = secondaryObject->getComponent<scene3d::MeshComponent>();
        if (!secondaryMesh)
            return fail("VIEWPORT-SCENE", "secondary object did not receive MeshComponent");
        const auto assignSecondaryResource = [&](const char* property, const char* resourceId) {
            editor::SetSceneComponentPropertyCommand command(property);
            command.objectId = secondaryObject->id().value();
            command.componentId = secondaryMesh->id().value();
            command.valueKind = editor::ScenePropertyValueKind::ResourceReference;
            command.stringValue = resourceId;
            return shell.submitCommand(std::move(command)).status
                == editor::EditorResponseStatus::Completed;
        };
        if (!assignSecondaryResource("resourceId", kViewportSmokeCubeMeshId)
            || !assignSecondaryResource("materialResourceId", kViewportSmokeCubeMaterialId))
            return fail("VIEWPORT-SCENE", "secondary resource assignment was rejected");
        const auto secondaryTransform =
            shell.submitCommand(editor::SelectSceneObjectCommand{secondaryObject->id().value()});
        editor::SetSceneObjectTransformCommand secondaryTrs;
        secondaryTrs.translation = {1.5f, 0.0f, 0.0f};
        secondaryTrs.rotation = core::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.6f);
        if (secondaryTransform.status != editor::EditorResponseStatus::Completed
            || shell.submitCommand(secondaryTrs).status != editor::EditorResponseStatus::Completed
            || shell.submitCommand(editor::SelectSceneObjectCommand{sceneObject->id().value()})
                    .status
                != editor::EditorResponseStatus::Completed)
            return fail("VIEWPORT-SCENE", "cannot place secondary viewport smoke object");

        if (viewportRuntimeSmoke) {
            auto& cameraObject = shell.sceneDocument().scene().createObject(
                core::pmr::String("ViewportCameraGizmo", &core::defaultCpuMemoryResource()));
            auto cameraDesc = scene::CameraComponentDesc::perspective(52.0f, 1.0f, 0.15f, 5.0f);
            cameraDesc.aspectPolicy = scene::CameraAspectPolicy::matchViewport();
            cameraObject.addComponent<scene::CameraComponent>(cameraDesc);
            cameraObject.transform().setLocalTranslation({-1.6f, 0.0f, 0.0f});
            auto& spotlightObject = shell.sceneDocument().scene().createObject(
                core::pmr::String("ViewportSpotGizmo", &core::defaultCpuMemoryResource()));
            scene3d::LightComponentDesc spotlightDesc;
            spotlightDesc.type = scene3d::LightType::Spot;
            spotlightDesc.range = 2.5f;
            spotlightDesc.spotConeAngle = 0.9f;
            spotlightObject.addComponent<scene3d::LightComponent>(spotlightDesc);
            spotlightObject.transform().setLocalTranslation({1.6f, 0.0f, 0.0f});
            editor::AddSceneComponentCommand addCollider("ColliderComponent");
            addCollider.objectId = secondaryObject->id().value();
            if (shell.submitCommand(std::move(addCollider)).status
                != editor::EditorResponseStatus::Completed)
                return fail("VIEWPORT-COLLIDER", "cannot add 3D Collider preview fixture");
            if (!tests::loadSpriteFixture(shell.projectResources())
                || !tests::loadTileFixture(shell.projectResources()))
                return fail("VIEWPORT-SPRITE", "cannot load deterministic Sprite/TileMap fixtures");
            if (shell.submitCommand(editor::CreateSceneObjectCommand("ViewportSprite")).status
                != editor::EditorResponseStatus::Completed)
                return fail("VIEWPORT-SPRITE", "cannot create Sprite viewport smoke object");
            auto* spriteObject = shell.sceneDocument().scene().findObjectByName(
                core::pmr::String("ViewportSprite", &core::defaultCpuMemoryResource()));
            editor::AddSceneComponentCommand addSprite("SpriteRendererComponent");
            addSprite.objectId = spriteObject ? spriteObject->id().value() : 0u;
            if (!spriteObject
                || shell.submitCommand(std::move(addSprite)).status
                    != editor::EditorResponseStatus::Completed)
                return fail("VIEWPORT-SPRITE", "cannot add SpriteRendererComponent by command");
            auto* spriteRenderer = spriteObject->getComponent<scene2d::SpriteRendererComponent>();
            if (!spriteRenderer)
                return fail("VIEWPORT-SPRITE", "SpriteRendererComponent was not created");
            spriteRenderer->setSpriteResource(
                core::pmr::String(tests::spriteA, &core::defaultCpuMemoryResource()));
            editor::SetSceneObjectTransformCommand spriteTransform;
            spriteTransform.objectId = spriteObject->id().value();
            spriteTransform.translation = {-1.5f, 0.0f, 0.0f};
            if (shell.submitCommand(spriteTransform).status
                != editor::EditorResponseStatus::Completed)
                return fail("VIEWPORT-SPRITE", "cannot position Sprite viewport smoke object");

            if (shell.submitCommand(editor::CreateSceneObjectCommand("ViewportTileMap")).status
                != editor::EditorResponseStatus::Completed)
                return fail("VIEWPORT-TILEMAP", "cannot create TileMap viewport smoke object");
            auto* tileMapObject = shell.sceneDocument().scene().findObjectByName(
                core::pmr::String("ViewportTileMap", &core::defaultCpuMemoryResource()));
            editor::AddSceneComponentCommand addTileMap("TileMapResourceComponent");
            addTileMap.objectId = tileMapObject ? tileMapObject->id().value() : 0u;
            editor::SetSceneComponentPropertyCommand tileMapResource("tileMapResourceId");
            tileMapResource.valueKind = editor::ScenePropertyValueKind::ResourceReference;
            tileMapResource.stringValue = tests::tileMapId;
            addTileMap.initialProperties.push_back(std::move(tileMapResource));
            editor::SetSceneComponentPropertyCommand tileMapChunkBudget("maxResidentChunks");
            tileMapChunkBudget.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
            tileMapChunkBudget.unsignedIntegerValue = 2u;
            addTileMap.initialProperties.push_back(std::move(tileMapChunkBudget));
            editor::SetSceneComponentPropertyCommand tileMapCellBudget("maxVisibleCells");
            tileMapCellBudget.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
            tileMapCellBudget.unsignedIntegerValue = 12u;
            addTileMap.initialProperties.push_back(std::move(tileMapCellBudget));
            editor::SetSceneComponentPropertyCommand tileMapObjectId("firstObjectId");
            tileMapObjectId.valueKind = editor::ScenePropertyValueKind::UnsignedInteger;
            tileMapObjectId.unsignedIntegerValue = 1000000u;
            addTileMap.initialProperties.push_back(std::move(tileMapObjectId));
            if (!tileMapObject
                || shell.submitCommand(std::move(addTileMap)).status
                    != editor::EditorResponseStatus::Completed)
                return fail("VIEWPORT-TILEMAP", "cannot add configured TileMap component");
            auto* tileMap = tileMapObject->getComponent<scene2d::TileMapResourceComponent>();
            if (tileMap == nullptr || tileMap->runtime() == nullptr)
                return fail("VIEWPORT-TILEMAP", "TileMap runtime fixture is unavailable");
        }

        if (viewportPipelineSmoke) {
            const auto targetExtent = resolveViewportExtent();
            if (targetExtent.width == 0u || targetExtent.height == 0u)
                return fail(
                    "VIEWPORT-TARGET", "scene workspace has no drawable viewport rectangle");
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
            if (capture)
                capture->StartFrameCapture(nullptr, nullptr);
#endif
            core::pmr::String viewportDiagnostics(&core::defaultCpuMemoryResource());
            const bool viewportInitialized = viewportSession.initialize(shell,
                *system.gpuDevice(),
                viewportArtifacts,
                viewportSpriteArtifacts,
                targetExtent,
                viewportDiagnostics);
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
            if (capture)
                capture->EndFrameCapture(nullptr, nullptr);
#endif
            if (!viewportInitialized)
                return fail("VIEWPORT-SESSION", viewportDiagnostics);
            auto oneDrawBudget = editor::kEditorSceneViewportSmokeSessionConfig;
            oneDrawBudget.maxDrawCalls = 1u;
            editor::EditorSceneViewportSession overBudgetSession(oneDrawBudget);
            core::pmr::String budgetDiagnostics(&core::defaultCpuMemoryResource());
            if (overBudgetSession.initialize(shell,
                    *system.gpuDevice(),
                    viewportArtifacts,
                    viewportSpriteArtifacts,
                    targetExtent,
                    budgetDiagnostics)
                || budgetDiagnostics != "ARP-EDITOR-VIEWPORT-SESSION-DRAW-BUDGET-EXCEEDED")
                return fail(
                    "VIEWPORT-BUDGET", "session did not reject a frame above its draw budget");
            const auto initialViewportVersion = shell.stateVersion();
            editor::SceneViewportCameraCommand orbit;
            orbit.operation = editor::SceneViewportCameraOperation::Orbit;
            orbit.deltaX = 4.0f;
            orbit.deltaY = 1.0f;
            orbit.viewportWidth = static_cast<float>(targetExtent.width);
            orbit.viewportHeight = static_cast<float>(targetExtent.height);
            if (shell.submitCommand(orbit).status != editor::EditorResponseStatus::Completed
                || shell.stateVersion() <= initialViewportVersion)
                return fail("VIEWPORT-SESSION",
                    "camera command did not advance the viewport state version");
            if (!viewportSession.renderIfDirty(shell, targetExtent, viewportDiagnostics))
                return fail("VIEWPORT-SESSION", viewportDiagnostics);
            const rhi::Extent2D resizedViewportExtent{
                targetExtent.width + 1u, targetExtent.height + 1u};
            if (!viewportSession.renderIfDirty(shell, resizedViewportExtent, viewportDiagnostics)
                || viewportSession.extent().width != resizedViewportExtent.width
                || viewportSession.extent().height != resizedViewportExtent.height
                || !viewportSession.renderIfDirty(shell, targetExtent, viewportDiagnostics)
                || viewportSession.extent().width != targetExtent.width
                || viewportSession.extent().height != targetExtent.height)
                return fail(
                    "VIEWPORT-SESSION", "resize did not recreate and restore the viewport target");
            std::printf("PASS: ARP Editor scene viewport session initialized at %ux%u\n",
                targetExtent.width,
                targetExtent.height);
            std::printf("PASS: ARP Editor viewport command dirty redraw and target resize\n");
            std::printf("PASS: ARP Editor viewport rejects draw count above session budget\n");
        }
    }
    renderer::GuiRenderer renderer(*system.gpuDevice());
    renderer::GuiRendererDesc desc;
    desc.shaderPath =
        (std::filesystem::path(ARP_SHADER_DIR) / "gui_overlay.slang").generic_string();
    desc.colorFormat = system.colorFormat();
    desc.extent = {unsigned(window.pixelWidth()), unsigned(window.pixelHeight())};
    desc.atlasTexture = atlas.atlas();
    desc.hasAtlasTexture = true;
    auto created = renderer.create(desc);
    if (!created.success)
        return fail("RENDERER", created.diagnostics);
    atlas.recordUploadsCompleted(atlas.pendingUploads().size());
    bool quit = false, dirty = true, minimized = false;
    unsigned rendered = 0;
    input::InputSystem inputSystem({});
    core::pmr::String viewportRuntimeDiagnostic(&core::defaultCpuMemoryResource());
    while (!quit) {
        const auto remoteStateVersion = shell.stateVersion();
        remoteServer.pump();
        if (shell.stateVersion() != remoteStateVersion)
            dirty = true;
        platform::WindowEventBatch batch;
        platform.pumpEvents(batch);
        for (auto event : batch.windowEvents) {
            if (event.type == platform::WindowEventType::CloseRequested)
                quit = true;
            if (event.type == platform::WindowEventType::Minimized)
                minimized = true;
            if (event.type == platform::WindowEventType::Restored) {
                minimized = false;
                dirty = true;
            }
            if (event.type == platform::WindowEventType::Resized && window.pixelWidth() > 0
                && window.pixelHeight() > 0) {
                system.waitGpuIdle();
                desc.extent = {unsigned(window.pixelWidth()), unsigned(window.pixelHeight())};
                auto resized = system.resize(desc.extent);
                if (!resized.success)
                    return fail("RESIZE", resized.diagnostics);
                auto guiResize = renderer.onResize(desc.extent);
                if (!guiResize.success)
                    return fail("GUI-RESIZE", guiResize.diagnostics);
                dirty = true;
            }
        }
        for (auto& event : batch.inputEvents)
            if (event.hasPosition || event.kind == platform::WindowInputEvent::Kind::PointerMoved) {
                event.x *= 1920.f / window.width();
                event.y *= 1080.f / window.height();
            }
        if (!batch.inputEvents.empty()) {
            platform::WindowEventBatch uiBatch;
            for (const auto& event : batch.windowEvents)
                uiBatch.windowEvents.push_back(event);
            for (const auto& event : batch.inputEvents) {
                shell.handlePlatformInput(event);
                const bool keyboard = event.kind == platform::WindowInputEvent::Kind::KeyDown
                    || event.kind == platform::WindowInputEvent::Kind::KeyUp
                    || event.kind == platform::WindowInputEvent::Kind::TextInput;
                if (!(keyboard && shell.scriptEditorCapturesKeyboard()))
                    uiBatch.inputEvents.push_back(event);
            }
            input::InputFrame frame;
            platform::applyWindowInputToInputFrame(uiBatch, frame);
            if (!frame.events().empty()) {
                auto routed = shell.runtime().dispatchFrame(frame, {window.id(), 512});
                if (!routed.ok())
                    return fail("INPUT", routed.diagnostic.message);
                auto begun = inputSystem.beginFrame(frame, routed.eventConsumption);
                if (!begun.ok())
                    return fail("INPUT-SYSTEM", begun.diagnostic().message);
                if (shell.diagnostic().code != ui::UiDiagnosticCode::None)
                    return fail("CONTROL", shell.diagnostic().message);
            }
            dirty = true;
        }
        if (quit)
            break;
        dirty = gamePreview.synchronize(shell, desc.extent.width, desc.extent.height, minimized)
            || dirty;
        if (!dirty || minimized) {
            // A small yield avoids spinning while this event-driven prototype is idle.
#if defined(__EMSCRIPTEN__)
            emscripten_sleep(8);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
#endif
            continue;
        }
        bool viewportAvailableThisFrame = false;
        if (viewportPipelineSmoke) {
            const auto viewportExtent = resolveViewportExtent();
            if (viewportExtent.width > 0u && viewportExtent.height > 0u) {
                core::pmr::String viewportDiagnostics(&core::defaultCpuMemoryResource());
                if (!viewportSession.renderIfDirty(shell, viewportExtent, viewportDiagnostics))
                    return fail("VIEWPORT-SESSION", viewportDiagnostics);
                viewportAvailableThisFrame = true;
            }
        } else if (shell.workspace() == "3D") {
            const auto viewportExtent = resolveViewportExtent();
            if (viewportExtent.width > 0u && viewportExtent.height > 0u) {
                const auto snapshot = shell.sceneDocument().scene().extractTraversalSnapshot(
                    scene::SceneTraversalOptions{1024u});
                const auto frame =
                    editor::buildEditorSceneViewportFrame(shell.sceneDocument().scene(),
                        snapshot,
                        shell.stateVersion(),
                        viewportExtent.width,
                        viewportExtent.height,
                        {0u, 0u, viewportExtent.width, viewportExtent.height});
                if (!frame.valid()) {
                    viewportRuntimeDiagnostic =
                        editor::editorSceneViewportDiagnosticCode(frame.validation.diagnostic);
                    shell.reportViewportDiagnostic(
                        editor::editorSceneViewportDiagnosticCode(frame.validation.diagnostic));
                } else if (!frame.frame.drawItems.empty()) {
                    const auto resolvedFrame = editor::resolveEditorSceneViewportResources(
                        frame.frame, shell.projectResources());
                    if (!resolvedFrame.valid()) {
                        viewportRuntimeDiagnostic = editor::editorSceneViewportDiagnosticCode(
                            resolvedFrame.frameDiagnostic);
                        shell.reportViewportDiagnostic(editor::editorSceneViewportDiagnosticCode(
                            resolvedFrame.frameDiagnostic));
                    } else {
                        core::pmr::String viewportDiagnostics(&core::defaultCpuMemoryResource());
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
                        if (viewportRuntimeSmoke && capture)
                            capture->StartFrameCapture(nullptr, nullptr);
#endif
                        const bool renderedViewport = viewportSession.valid()
                            ? viewportSession.renderIfDirty(
                                  shell, viewportExtent, viewportDiagnostics)
                            : viewportSession.initialize(shell,
                                  *system.gpuDevice(),
                                  viewportArtifacts,
                                  viewportSpriteArtifacts,
                                  viewportExtent,
                                  viewportDiagnostics);
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
                        if (viewportRuntimeSmoke && capture)
                            capture->EndFrameCapture(nullptr, nullptr);
#endif
                        if (!renderedViewport) {
                            viewportRuntimeDiagnostic = viewportDiagnostics;
                            shell.reportViewportDiagnostic(viewportDiagnostics);
                        } else {
                            viewportRuntimeDiagnostic.clear();
                            viewportAvailableThisFrame = true;
                        }
                    }
                }
            }
        }
        authored =
            shell.drawCommands(viewportAvailableThisFrame ? editor::kEditorSceneViewportTextureId
                                                          : std::string_view{});
        if (!authored.ok())
            return fail("DRAW", authored.diagnostics());
        if (viewportPipelineSmoke
            && std::none_of(
                authored.commands().begin(), authored.commands().end(), [](const auto& command) {
                    return command.kind == renderer::ui::UiDrawCommandKind::Image
                        && command.textureSource == renderer::ui::UiTextureSource::External
                        && command.textureResourceId == editor::kEditorSceneViewportTextureId;
                }))
            return fail("VIEWPORT-COMPOSITE",
                "3D workspace did not author the external scene viewport image");
        if (viewportRuntimeSmoke && !viewportAvailableThisFrame)
            return fail("VIEWPORT-RUNTIME-SMOKE",
                viewportRuntimeDiagnostic.empty()
                    ? core::pmr::String("ordinary 3D workspace did not render its scene",
                          &core::defaultCpuMemoryResource())
                    : viewportRuntimeDiagnostic);
        auto resolved = atlas.resolve(authored.commands());
        if (!resolved.ok())
            return fail("GLYPHS", resolved.diagnostics());
        if (!atlas.pendingUploads().empty()) {
            auto uploads = atlas.combinedAtlasPendingUploads();
            auto updated = renderer.updateAtlasTexture(atlas.atlas(), uploads);
            if (!updated.success)
                return fail("GLYPH-UPLOAD", updated.diagnostics);
            atlas.recordUploadsCompleted(uploads.size());
        }
        auto commands = resolved.commands();
        float sx = desc.extent.width / 1920.f, sy = desc.extent.height / 1080.f;
        for (auto& c : commands) {
            c.rect.x *= sx;
            c.rect.width *= sx;
            c.rect.y *= sy;
            c.rect.height *= sy;
            c.clipRect.x *= sx;
            c.clipRect.width *= sx;
            c.clipRect.y *= sy;
            c.clipRect.height *= sy;
        }
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
        if (capture && rendered == 0)
            capture->StartFrameCapture(nullptr, nullptr);
#endif
        auto* swapchain = system.swapchain();
        auto acquired = swapchain->acquireNextFrame();
        if (!acquired.success)
            return fail("ACQUIRE", acquired.diagnostics);
        auto image = swapchain->acquiredImage(acquired);
        if (!image.success || !image.texture) {
            swapchain->discardFrame(acquired);
            return fail("IMAGE", image.diagnostics);
        }
        render_graph::FrameGraph graph(*system.gpuDevice());
        auto target = graph.importSwapchainTexture(std::move(image.texture), image.ref);
        renderer::RendererResult recorded;
        graph.addPass("ArpEditorUI")
            .writeAttachment(target,
                rhi::ResourceState::ColorAttachment,
                rhi::LoadOp::Clear,
                rhi::StoreOp::Store,
                {},
                {0.08f, 0.08f, 0.08f, 1})
            .execute([&](render_graph::RenderContext& ctx) {
                renderer::RenderFrameContext frame;
                frame.frame = acquired;
                frame.colorTexture = &graph.texture(target);
                frame.colorTarget = {"EditorSwapchain",
                    graph.texture(target).id(),
                    desc.extent,
                    system.colorFormat(),
                    rhi::ResourceState::Present};
                if (viewportSession.valid()) {
                    core::pmr::Vector<renderer::GuiExternalTextureSetBinding> bindings{
                        &core::defaultCpuMemoryResource()};
                    bindings.push_back(viewportSession.guiBinding());
                    recorded =
                        renderer.recordPassWithExternalTextures(ctx, frame, commands, bindings);
                } else {
                    recorded = renderer.recordPass(ctx, frame, commands);
                }
            });
        graph.execute();
        if (!recorded.success) {
            swapchain->discardFrame(acquired);
            return fail("PASS", recorded.diagnostics);
        }
        auto presented = swapchain->present(acquired);
        if (!presented.success)
            return fail("PRESENT", presented.diagnostics);
        // The prototype redraws on input; completion keeps dynamic atlas updates safe.
        system.waitGpuIdle();
#if defined(ARP_RENDERDOC_APP_AVAILABLE)
        if (capture && rendered == 0)
            capture->EndFrameCapture(nullptr, nullptr);
#endif
        ++rendered;
        dirty = false;
        if (hidden) {
            std::printf("PASS: ARP Editor hidden frame: %zu controls, %zu draws, selection=%s\n",
                shell.runtime().controlCount(),
                commands.size(),
                shell.selection().c_str());
            quit = true;
        }
    }
    system.waitGpuIdle();
    return 0;
}

} // namespace arp::editor

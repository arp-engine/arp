#include "3d/components.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "editor/viewport/editor_scene_viewport_frame_builder.h"
#include "editor/viewport/editor_scene_viewport_mesh_cache.h"
#include "editor/viewport/editor_scene_viewport_resource_resolver.h"
#include "editor/viewport/editor_scene_viewport_texture_cache.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_scene_document.h"
#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_terrain_browser.h"
#include "editor/workspace/editor_terrain_preview.h"
#include "resource/terrain_asset_resource.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>

using namespace arp;

namespace {
constexpr const char* terrainId = "7c023e4a-b5b6-4444-8123-100000000001";
constexpr const char* previewId = "7c023e4a-b5b6-4444-8123-100000000002";
constexpr const char* semantics[]{"grassMoss",
    "forestFloor",
    "wetMud",
    "rockyPath",
    "mossyRock",
    "dirtySnow",
    "snowOverlay",
    "wetnessOverlay"};

void write(const std::filesystem::path& path, const core::pmr::String& contents)
{
    std::ofstream stream(path);
    stream << contents;
    assert(stream.good());
}

void check(const std::filesystem::path& root, const char* terrain, const char* preview)
{
    editor::EditorProjectResources owner;
    const auto loaded = owner.load(root);
    if (!loaded.success)
        std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    auto& manager = owner.manager();
    editor::EditorTerrainBrowser browser;
    assert(browser.refresh(manager) && browser.entries().size() == 1);
    const auto& summary = browser.entries().front();
    assert(summary.terrainId == terrain && summary.previewId == preview && summary.sourceBytes > 0
        && summary.layerCount == 8);
    const auto handle = manager.find<resource::TerrainAssetResource>(core::pmr::String(terrain));
    assert(handle.ready());
    const auto rootMaterial = manager.find<resource::MaterialResource>(core::pmr::String(preview));
    const auto* projection =
        dynamic_cast<const editor::EditorTerrainPreviewMaterial*>(rootMaterial.get());
    assert(projection
        && projection->tiles.size()
            == std::size_t(handle.get()->tileCountX()) * handle.get()->tileCountZ());
    editor::EditorSceneDocument document;
    auto& scene = document.scene();
    auto* mesh =
        scene.findObject(document.selectedObject())->getComponent<scene3d::MeshComponent>();
    assert(mesh);
    mesh->set_enabled(false);
    auto& object = scene.createObject(core::pmr::String("Terrain"));
    auto& component = object.addComponent<scene3d::TerrainComponent>();
    component.set_resource_id(core::pmr::String(terrain));
    component.set_material_resource_id(core::pmr::String(preview));
    const auto frame = editor::buildEditorSceneViewportFrame(scene,
        scene.extractTraversalSnapshot({}),
        1,
        640,
        480,
        {0, 0, 640, 480},
        &manager,
        projection->tiles.size());
    assert(frame.valid() && frame.frame.drawItems.size() == projection->tiles.size()
        && frame.frame.bakedTerrainPreview);
    const auto resolved = editor::resolveEditorSceneViewportResources(frame.frame, manager);
    assert(resolved.valid());
    auto device = rhi::createNullDevice();
    editor::EditorSceneViewportMeshCache geometry({projection->tiles.size(), 64u * 1024u * 1024u});
    assert(geometry.acquire(*device, manager, resolved.items.front()).valid());
    editor::EditorSceneViewportTextureCache textures({1, 16u * 1024u * 1024u, 16u * 1024u * 1024u});
    assert(textures
            .acquire(*device,
                manager,
                resolved.items.front().normalTextureId,
                resolved.items.front().normalTextureGeneration,
                editor::EditorSceneViewportTextureColorSpace::Linear)
            .valid());
    auto forged = frame.frame;
    forged.drawItems.front().terrainPreviewMaterialResourceId.clear();
    assert(!editor::resolveEditorSceneViewportResources(forged, manager).valid());
    forged = frame.frame;
    forged.drawItems.front().materialResourceId = core::pmr::String(preview);
    assert(!editor::resolveEditorSceneViewportResources(forged, manager).valid());
    for (const auto& tile : projection->tiles) {
        assert(editor::resolveEditorTerrainPreviewMaterial(manager,
                   core::pmr::String(terrain),
                   handle.generation(),
                   core::pmr::String(preview),
                   tile.coord)
            == tile.materialId);
        const auto material = manager.find<resource::MaterialResource>(tile.materialId);
        const auto image =
            manager.find<resource::TextureResource>(material.get()->baseColorTextureId);
        assert(image.ready() && image.get()->decodedWidth == projection->resolution
            && image.get()->decodedRgba8.size()
                == std::size_t(projection->resolution) * projection->resolution * 4);
        assert(editor::resolveEditorTerrainPreviewMaterial(manager,
            core::pmr::String(terrain),
            handle.generation() + 1,
            core::pmr::String(preview),
            tile.coord)
                .empty());
    }
    const auto* stableOwner = &owner.manager();
    const auto bad = owner.loadManifest(root,
        "format: ArpEditorProject\nversion: 1\nassetPackages: []\nterrainPreviews: [missing.yaml]\n");
    assert(!bad.success && &owner.manager() == stableOwner);
    assert(
        owner.manager().find<resource::TerrainAssetResource>(core::pmr::String(terrain)).ready());
    const auto first = projection->tiles.front();
    const auto firstMaterial = manager.find<resource::MaterialResource>(first.materialId);
    const auto normalId = firstMaterial.get()->normalTextureId;
    const auto normal = manager.find<resource::TextureResource>(normalId);
    auto replacement = core::makeUnique<resource::TextureResource>();
    replacement->configureMetadata(normal.get()->reference());
    replacement->sourceResourceId = normal.get()->sourceResourceId;
    replacement->usage = normal.get()->usage;
    replacement->srgb = normal.get()->srgb;
    replacement->decodedSrgb = normal.get()->decodedSrgb;
    replacement->decodedWidth = normal.get()->decodedWidth;
    replacement->decodedHeight = normal.get()->decodedHeight;
    replacement->decodedRgba8 = normal.get()->decodedRgba8;
    assert(manager.unload(normalId).success);
    assert(manager.loadResource(std::move(replacement)).success);
    assert(manager.find<resource::TextureResource>(normalId).generation() != normal.generation());
    assert(!browser.refresh(manager) && browser.entries().empty() && !browser.diagnostic().empty());
    assert(editor::resolveEditorTerrainPreviewMaterial(manager,
        core::pmr::String(terrain),
        handle.generation(),
        core::pmr::String(preview),
        first.coord)
            .empty());
    assert(!editor::resolveEditorSceneViewportResources(frame.frame, manager).valid());
    assert(manager.unload(first.materialId).success);
    assert(editor::resolveEditorTerrainPreviewMaterial(manager,
        core::pmr::String(terrain),
        handle.generation(),
        core::pmr::String(preview),
        first.coord)
            .empty());
    geometry.clearAfterDeviceIdle();
    textures.clearAfterDeviceIdle();
    std::printf("PASS: terrain preview preparation, %zu tiles, %llu omitted vegetation instances\n",
        projection->tiles.size(),
        static_cast<unsigned long long>(projection->vegetationInstanceCount));
}

void checkBrowser(const std::filesystem::path& root)
{
    editor::EditorShell shell;
    assert(shell.load(std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", root)
            .ok());
    const auto count = shell.sceneDocument().scene().objects().size();
    editor::OpenTerrainPreviewCommand open;
    open.previewId = previewId;
    open.expectedGeneration =
        shell.projectResources().find<resource::MaterialResource>(open.previewId).generation();
    auto stale = open;
    ++stale.expectedGeneration;
    const auto version = shell.stateVersion();
    assert(shell.submitCommand(stale).status == editor::EditorResponseStatus::Failed);
    assert(
        shell.stateVersion() == version && shell.sceneDocument().scene().objects().size() == count);
    assert(shell.runtime()
            .interact(ui::UiControlInteraction::click(shell.id("files_terrain_assets")))
            .ok());
    auto find = [&](std::string_view name) {
        for (const auto id : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(id);
            if (widget && widget->name() == name)
                return id;
        }
        return ui::WidgetId{};
    };
    const auto panel = find("terrain_browser_panel");
    assert(panel.valid() && shell.runtime().canvas().findWidget(panel)->visible());
    editor::SelectTerrainPreviewCommand select;
    select.previewId = open.previewId;
    select.expectedGeneration = open.expectedGeneration + 1;
    const auto beforeSelection = shell.stateVersion();
    assert(shell.submitCommand(select).status == editor::EditorResponseStatus::Failed);
    assert(shell.stateVersion() == beforeSelection);
    select.expectedGeneration = open.expectedGeneration;
    assert(shell.submitCommand(select).status == editor::EditorResponseStatus::Completed);
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto button = find("terrain_browser_open");
    assert(layout.ok() && layout.clipRect(button).height > 0);
    const auto* grid = shell.runtime().canvas().findWidget(find("terrain_browser_grid"));
    assert(grid && grid->desc().text.find("2 x 1 tiles") != core::pmr::String::npos);
    assert(shell.submitCommand(editor::ActivateControlCommand("workspace_3D")).status
        == editor::EditorResponseStatus::Completed);
    const auto beforeCamera = shell.sceneViewportCamera().distance();
    editor::EditorInputCommand gesture;
    gesture.kind = editor::EditorInputKind::PointerDown;
    gesture.primaryButton = true;
    gesture.x = 700;
    gesture.y = 700;
    assert(shell.submitCommand(gesture).status == editor::EditorResponseStatus::Completed);
    gesture.kind = editor::EditorInputKind::PointerUp;
    assert(shell.submitCommand(gesture).status == editor::EditorResponseStatus::Completed);
    assert(shell.sceneViewportCamera().distance() == beforeCamera);
    const auto draws = shell.drawCommands();
    assert(draws.ok() && !draws.commands().empty());
    const auto* lastDrawWidget =
        shell.runtime().canvas().findWidget(draws.commands().back().widgetId);
    assert(lastDrawWidget && lastDrawWidget->name().find("terrain_browser_") == 0);
    assert(shell.runtime().interact(ui::UiControlInteraction::click(button)).ok());
    assert(!shell.runtime().canvas().findWidget(panel)->visible());
    assert(shell.sceneDocument().scene().objects().size() == count + 1);
    const auto selected = shell.sceneDocument().selectedObject();
    const auto* terrain = shell.sceneDocument()
                              .scene()
                              .findObject(selected)
                              ->getComponent<scene3d::TerrainComponent>();
    assert(terrain && terrain->resourceId() == terrainId
        && terrain->materialResourceId() == previewId);
    assert(shell.submitCommand(open).status == editor::EditorResponseStatus::Completed);
    assert(shell.sceneDocument().selectedObject() == selected
        && shell.sceneDocument().scene().objects().size() == count + 1);
    core::pmr::String yaml, diagnostic;
    assert(editor::EditorSceneAuthoringCodec::encode(shell.sceneDocument(), yaml, diagnostic));
    editor::EditorSceneDocument reopened(false);
    reopened.setResourceManagerProvider([&] {
        return &shell.projectResources();
    });
    const auto validator = [](void* context, std::string_view type, std::string_view id) {
        const auto* manager = static_cast<resource::ResourceManager*>(context);
        const auto* record = manager->recordSnapshot(id);
        return record && record->type == type && record->state == resource::ResourceState::Ready;
    };
    assert(editor::EditorSceneAuthoringCodec::decode(
        yaml, reopened, validator, &shell.projectResources(), diagnostic));
    const auto* reopenedTerrain =
        reopened.scene().findObject(selected)->getComponent<scene3d::TerrainComponent>();
    assert(reopenedTerrain && reopenedTerrain->resourceId() == terrainId
        && reopenedTerrain->materialResourceId() == previewId);
    assert(shell.submitCommand(editor::ShowTerrainBrowserCommand{}).status
        == editor::EditorResponseStatus::Completed);
    assert(shell.submitCommand(editor::ShowTerrainBrowserCommand{false}).status
        == editor::EditorResponseStatus::Completed);

    const auto emptyRoot = root / "empty-project";
    std::filesystem::create_directory(emptyRoot);
    editor::EditorShell empty;
    assert(empty
            .load(
                std::filesystem::path(ARP_SOURCE_DIR) / "editor/resources/editor.arpui", emptyRoot)
            .ok());
    assert(empty.submitCommand(editor::ShowTerrainBrowserCommand{}).status
        == editor::EditorResponseStatus::Completed);
    bool emptyDisabledOpen = false;
    for (const auto id : empty.runtime().canvas().traversal()) {
        const auto* widget = empty.runtime().canvas().findWidget(id);
        if (widget && widget->name() == "terrain_browser_open")
            emptyDisabledOpen = !widget->enabled();
    }
    assert(emptyDisabledOpen);
}

void checkCamera()
{
    editor::EditorSceneViewportCamera camera;
    for (const float aspect : {0.5f, 1.0f, 2.0f}) {
        assert(camera.frameBounds({-300, -10, -300}, {300, 110, 300}, aspect));
        for (const float x : {-300.f, 300.f})
            for (const float y : {-10.f, 110.f})
                for (const float z : {-300.f, 300.f}) {
                    float sx, sy;
                    assert(camera.project({x, y, z}, 1000 * aspect, 1000, sx, sy));
                    assert(sx > 0 && sx < 1000 * aspect && sy > 0 && sy < 1000);
                }
    }
    const auto distance = camera.distance();
    const auto target = camera.target();
    assert(!camera.frameBounds({0, 0, 0}, {0, 0, 0}, 1));
    assert(!camera.frameBounds({1, 0, 0}, {0, 1, 1}, 1));
    assert(!camera.frameBounds({0, 0, 0}, {10000, 10000, 10000}, 1));
    assert(!camera.frameBounds({0, 0, 0}, {1, 1, 1}, std::numeric_limits<float>::quiet_NaN()));
    assert(camera.distance() == distance && core::distance(camera.target(), target) == 0);
}
} // namespace

int main(int argc, char** argv)
{
    checkCamera();
    if (argc == 4) {
        check(argv[1], argv[2], argv[3]);
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path()
        / ("arp-editor-terrain-preview-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    std::filesystem::copy_file(
        std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures/scene_2d/glyph.png",
        root / "map.png");
    resource::TerrainAssetBuildDesc desc;
    desc.version = resource::TerrainAssetVersion::V2;
    desc.resourceId = terrainId;
    desc.hasVegetation = false;
    desc.bounds = {0, 4, 0, 2};
    desc.tileCountX = 2;
    desc.tileCountZ = 1;
    desc.samplesPerEdge = 2;
    desc.tileWorldSize = 2;
    for (std::size_t i = 0; i < 8; ++i) {
        desc.layers[i].layerId = desc.layers[i].semantic = semantics[i];
        desc.layers[i].page = i / 4;
        desc.layers[i].channel = i % 4;
        desc.layers[i].materialResourceId =
            resource::deriveResourceUuid(previewId, "layer", std::min(i, std::size_t(5)));
    }
    for (std::int32_t x = 0; x < 2; ++x) {
        resource::TerrainAssetBuildTile tile;
        tile.coord = {x, 0};
        tile.heights = {0, 1, 0, 1};
        if (x == 1)
            tile.heights = {1, 2, 1, 2};
        tile.weightPageA = {255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0};
        tile.weightPageB.resize(16);
        desc.tiles.push_back(std::move(tile));
    }
    assert(resource::TerrainAssetWriter().write(desc, root / "terrain").success);
    using Yaml = core::YamlContract;
    auto config = Yaml::Node::mapping();
    config["type"] = "EditorTerrainPreview";
    config["contractVersion"] = "editor.terrain-preview/v1";
    config["resourceId"] = previewId;
    config["terrainId"] = terrainId;
    config["terrain"] = "terrain/terrain.asset.yaml";
    config["vegetationPreview"] = "omit-with-diagnostic";
    config["resolution"] = 4;
    config["snowLine"] = 60.0;
    config["snowFeather"] = 20.0;
    config["maxFileBytes"] = 1024 * 1024;
    config["maxDecodedBytes"] = 1024 * 1024;
    config["maxPreviewBytes"] = 640;
    config["vegetation"] = Yaml::Node::mapping();
    config["vegetation"]["input"] = "absent";
    config["vegetation"]["resourceId"] = "";
    config["vegetation"]["catalog"] = "";
    config["layers"] = Yaml::Node::sequence();
    for (std::size_t i = 0; i < 6; ++i) {
        auto layer = Yaml::Node::mapping();
        layer["semantic"] = semantics[i];
        layer["materialId"] = desc.layers[i].materialResourceId.c_str();
        layer["physicalSizeMeters"] = 2.0;
        layer["normalConvention"] = "opengl";
        for (const auto* map : {"albedo", "normal", "roughness", "occlusion"}) {
            layer[map] = Yaml::Node::mapping();
            layer[map]["path"] = "map.png";
            layer[map]["mime"] = "image/png";
            layer[map]["colorSpace"] = map == std::string_view("albedo") ? "srgb" : "linear";
        }
        config["layers"].get_value_ref<Yaml::Node::sequence_type&>().push_back(std::move(layer));
    }
    write(root / "preview.yaml", Yaml::serialize(config));
    write(root / "project.arp.yaml",
        core::pmr::String(
            "format: ArpEditorProject\nversion: 1\nassetPackages: []\nterrainPreviews: [preview.yaml]\n"));
    check(root, terrainId, previewId);
    checkBrowser(root);
    editor::EditorProjectResources owner;
    config["maxPreviewBytes"] = 639;
    write(root / "preview.yaml", Yaml::serialize(config));
    assert(!owner.load(root).success);
    config["maxPreviewBytes"] = 640;
    config["layers"][0]["albedo"]["path"] = "../map.png";
    write(root / "preview.yaml", Yaml::serialize(config));
    assert(!owner.load(root).success);
    std::filesystem::remove_all(root);
}

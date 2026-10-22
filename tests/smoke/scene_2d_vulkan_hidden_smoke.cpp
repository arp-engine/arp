#include "2d/scene_component_persistence.h"
#include "2d/sprite_animator.h"
#include "2d/tile_map_resource_component.h"
#include "platform/render_system.h"
#include "renderer/scene_2d_pipeline.h"
#include "scene/scene_asset_resource.h"
#include "tests/fixtures/physics2d_scene.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/scene_2d_fixture.h"
#include "tests/fixtures/tile_map_fixture.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
arp::core::UniquePtr<arp::scene::Scene> persistedScene(const arp::scene::Scene& source,
    arp::resource::ResourceManager& resources,
    arp::core::SharedPtr<arp::physics2d::PhysicsWorld2D> world)
{
    using namespace arp;
    scene::SceneComponentFactory factory;
    assert(scene2d::registerScene2DPersistence(factory, resources, std::move(world)));
    const core::pmr::String id = "00000000-0000-4000-8000-000000009999";
    core::pmr::String yaml, diagnostic;
    if (!scene::SceneDocumentResource::encode(
            source, factory, "arp.scene-asset", id, yaml, diagnostic)) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return {};
    }
    const auto folder = std::filesystem::current_path() / "build" / "scene-reflection-smoke";
    std::filesystem::create_directories(folder);
    const auto file = folder / "scene.yaml";
    {
        std::ofstream stream(file, std::ios::binary);
        stream.write(yaml.data(), yaml.size());
    }
    const auto loaded = resources.load<scene::SceneAssetResource>(
        {}, id, core::toPmrString(file.string()), 262144, 128);
    if (!loaded.success) {
        std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
        return {};
    }
    auto candidate = resources.find<scene::SceneAssetResource>(id).get()->instantiate(
        resources, factory, diagnostic);
    if (!candidate)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(resources.unload(id).success);
    return candidate;
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    resource::ResourceManager resources;
    if (!tests::loadSpriteFixture(resources))
        return 1;
    const bool physics = argc == 2 && std::strcmp(argv[1], "--physics") == 0;
    const bool animation = argc == 2 && std::strcmp(argv[1], "--animation") == 0;
    const bool tiles = argc == 2 && std::strcmp(argv[1], "--tiles") == 0;
    tests::Physics2DShowcase showcase;
    scene::Scene spriteScene;
    core::pmr::Vector<scene2d::SpriteDrawItem> items;
    for (std::uint32_t i = 0; i < 5; ++i) {
        scene2d::SpriteDrawItem item;
        item.sprite =
            resources.find<scene2d::SpriteResource>(i == 2 ? tests::spriteB : tests::spriteA);
        item.sorting.objectId = i + 1;
        item.sorting.ySort = true;
        item.world.translation = {float(i) * 1.2f - 2.5f, 0, 0};
        item.sorting.worldY = 0;
        item.world.scale = {2, 2, 1};
        item.flipX = i == 1;
        item.flipY = i == 3;
        if (i == 4)
            item.world.rotation = {0, 0, .38268343f, .92387953f};
        items.push_back(item);
        auto& object = spriteScene.createObject("sprite smoke");
        object.set_local_transform(item.world);
        scene2d::SpriteRendererComponentDesc sprite;
        sprite.spriteResourceId = item.sprite.id();
        sprite.ySort = item.sorting.ySort;
        sprite.flipX = item.flipX;
        sprite.flipY = item.flipY;
        object.addComponent<scene2d::SpriteRendererComponent>(sprite);
    }
    if (physics) {
        assert(showcase.create());
        for (int i = 0; i < 120; ++i)
            assert(showcase.step());
    }
    if (animation) {
        core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
        frames.push_back({core::pmr::String(tests::spriteA), .125, 1});
        frames.push_back({core::pmr::String(tests::spriteB), .125, 2});
        const core::pmr::String animationId = "00000000-0000-4000-8000-000000000010";
        assert(resources.load<scene2d::SpriteAnimationResource>({}, animationId, frames, true)
                .success);
        scene2d::SpriteAnimator animator(8);
        assert(animator.play(resources.find<scene2d::SpriteAnimationResource>(animationId)));
        assert(animator.advance(.125) && animator.frame() == 1);
        for (auto& object : spriteScene.objects())
            if (auto* sprite = object->getComponent<scene2d::SpriteRendererComponent>())
                sprite->setSpriteResource(*animator.spriteId());
    }
    auto restoredScene =
        persistedScene(physics ? showcase.scene : spriteScene, resources, showcase.world);
    assert(restoredScene);
    auto& scene = *restoredScene;
    scene2d::SceneSpriteExtractor extractor(resources, 16);
    assert(extractor.extract(scene, scene.extractTraversalSnapshot({16})));
    scene2d::SpriteDrawPlan plan(16);
    auto tilePhysics = core::makeShared<physics2d::PhysicsWorld2D>(
        physics2d::WorldConfig{{0, 0}, 8, 32, 4, 64, 64, 32, 16, 4, 1.f / 60.f});
    if (tiles)
        assert(tests::loadTileFixture(resources));
    core::UniquePtr<scene::Scene> restoredTiles;
    scene2d::TileMapRuntime* tileRuntime = nullptr;
    if (tiles) {
        scene::Scene tileScene;
        tileScene.createObject("tiles").addComponent<scene2d::TileMapResourceComponent>(resources,
            resources.find<scene2d::TileMapResource>(tests::tileMapId),
            tilePhysics,
            scene2d::TileMapRuntimeDesc{2, 12, 100});
        restoredTiles = persistedScene(tileScene, resources, tilePhysics);
        assert(restoredTiles);
        tileRuntime = restoredTiles->findObjectByName("tiles")
                          ->getComponent<scene2d::TileMapResourceComponent>()
                          ->runtime();
        assert(tileRuntime && tileRuntime->updateResidency({-2, -1, 4, 2})
            && tileRuntime->extract(.125));
    }
    const auto& draws = tiles ? tileRuntime->items() : extractor.items();
    if (!plan.build(resources, draws)) {
        std::fprintf(stderr, "%s\n", plan.diagnostic());
        return 1;
    }
    platform::WindowSystem platform;
    if (!platform.initialize().success)
        return 1;
    auto window = platform.createWindow({"Scene2D Vulkan", 1280, 720, true, true});
    if (!window.valid())
        return 1;
    auto system = platform::createDefaultRenderSystem(platform, window, "Scene2D");
    if (!system.valid() || system.backend() != rhi::Backend::Vulkan) {
        std::fprintf(stderr, "%s\n", system.diagnostics().c_str());
        return 1;
    }
    auto& device = *system.gpuDevice();
    renderer::Scene2DRendererDesc desc;
    desc.shaderPath = ARP_SHADER_DIR "/scene_2d_sprites.slang";
    desc.colorFormat = system.colorFormat();
    desc.maxInstances = 16;
    desc.maxAtlases = 2;
    desc.frameSlots = 2;
    renderer::Scene2DRenderPipeline renderer(resources, desc);
    auto result =
        renderer.create({device, system.colorFormat(), rhi::TextureFormat::Unknown, {1280, 720}});
    if (!result.success) {
        std::fprintf(stderr, "create: %s\n", result.diagnostics.c_str());
        return 1;
    }
    for (auto id : {tests::atlasA, tests::atlasB}) {
        result = renderer.prepareAtlas(resources.find<scene2d::SpriteAtlasResource>(id));
        if (!result.success) {
            std::fprintf(stderr, "atlas: %s\n", result.diagnostics.c_str());
            return 1;
        }
    }
    scene2d::Viewport2DMapping mapping;
    assert(mapping.configure(1280, 720, {1280, 720}, false));
    tests::RenderDocCaptureScope capture;
    auto frame = system.swapchain()->acquireNextFrame();
    if (!frame.success)
        return 1;
    auto image = system.swapchain()->acquiredImage(frame);
    if (!image.success || !image.texture)
        return 1;
    renderer::Scene2DFramePayload payload{&plan, mapping, {}, 80};
    renderer::RenderFrameRequest request;
    request.pipelinePayload = &payload;
    request.pipelinePayloadContractName = renderer::Scene2DFramePayload::contract;
    request.pipelinePayloadContractVersion = renderer::Scene2DFramePayload::version;
    renderer::RenderPipelineContext context{
        frame, {}, nullptr, image.texture.get(), nullptr, request};
    result = renderer.render(context);
    if (!result.success) {
        std::fprintf(stderr, "draw: %s\n", result.diagnostics.c_str());
        return 1;
    }
    const auto presented = system.swapchain()->present(frame);
    if (!presented.success) {
        std::fprintf(stderr, "present: %s\n", presented.diagnostics.c_str());
        return 1;
    }
    system.waitGpuIdle();
    capture.end();
    assert(renderer.lastDrawCount() == (physics || animation ? 1u : 3u));
    assert(resources.unload(tests::atlasA).success);
    assert(!plan.build(resources, extractor.items()) || animation);
    std::printf("PASS: Vulkan %s, scene extraction, %zu instances, %u batches, atlas lifecycle\n",
        tiles           ? "tiles"
            : physics   ? "physics"
            : animation ? "animation"
                        : "sprites",
        draws.size(),
        renderer.lastDrawCount());
}

#include "2d/bounds.h"
#include "2d/camera_2d.h"
#include "2d/scene_sprite_extractor.h"
#include "2d/sprite_vertex_draw_plan.h"
#include "tests/fixtures/scene_2d_fixture.h"

#include <cmath>

int main()
{
    using namespace arp;
    resource::ResourceManager manager;
    assert(tests::loadSpriteFixture(manager));
    auto a = manager.find<scene2d::SpriteResource>(tests::spriteA);
    auto b = manager.find<scene2d::SpriteResource>(tests::spriteB);
    assert(a.ready() && b.ready());
    scene::Scene scene;
    auto& object = scene.createObject("sprite");
    scene2d::SpriteRendererComponentDesc description;
    description.spriteResourceId = tests::spriteA;
    object.addComponent<scene2d::SpriteRendererComponent>(description);
    scene2d::SceneSpriteExtractor extractor(manager, 4);
    assert(extractor.extract(scene, scene.extractTraversalSnapshot({4}))
        && extractor.items().size() == 1);
    scene2d::Camera2D camera;
    core::Vec2 aligned;
    assert(camera.setPosition({.123f, .234f}) && camera.alignedPosition(16, 2, aligned));
    assert(aligned.x == .125f && aligned.y == .21875f);
    assert(camera.follow({10, 20}, 1, 1) && camera.position().x > 6);
    assert(!manager.find<scene2d::SpriteResource>(tests::atlasA).ready());
    assert(!manager.find<scene2d::SpriteResource>("00000000-0000-4000-8000-000000000099").ready());
    core::pmr::Vector<scene2d::SpriteDrawItem> items;
    for (std::uint32_t i = 0; i < 3; ++i) {
        scene2d::SpriteDrawItem item;
        item.sprite = i == 1 ? b : a;
        item.sorting.objectId = i + 1;
        items.push_back(item);
    }
    scene2d::SpriteDrawPlan plan(3);
    assert(plan.build(manager, items));
    const auto warmed = core::defaultCpuAllocator().stats().allocationCount;
    for (int frame = 0; frame < 100; ++frame)
        assert(plan.build(manager, items));
    assert(core::defaultCpuAllocator().stats().allocationCount == warmed);
    assert(plan.batches().size() == 3); // A B A cannot be merged across transparency order.
    assert(plan.instances()[0].origin.x == -.5f && plan.instances()[0].uv.z == .5f);
    scene2d::SpriteVertexDrawPlan vertices({1, 3, 3 * 6 * sizeof(scene2d::SpriteVertex)});
    assert(
        vertices.build(plan) && vertices.vertices().size() == 18 && vertices.batches().size() == 3);
    assert(vertices.batches()[0].atlas.get() == a.get()->atlas().get()
        && vertices.batches()[1].atlas.get() == b.get()->atlas().get()
        && vertices.batches()[2].atlas.get() == a.get()->atlas().get());
    for (std::uint32_t i = 0; i < 3; ++i)
        assert(
            vertices.batches()[i].firstVertex == i * 6 && vertices.batches()[i].vertexCount == 6);
    const std::array<core::Vec2, 6> expectedPositions{core::Vec2{-.5f, -.5f},
        core::Vec2{.5f, -.5f},
        core::Vec2{.5f, .5f},
        core::Vec2{-.5f, -.5f},
        core::Vec2{.5f, .5f},
        core::Vec2{-.5f, .5f}};
    const std::array<core::Vec2, 6> expectedUvs{core::Vec2{0, 1},
        core::Vec2{.5f, 1},
        core::Vec2{.5f, 0},
        core::Vec2{0, 1},
        core::Vec2{.5f, 0},
        core::Vec2{0, 0}};
    for (std::size_t i = 0; i < expectedPositions.size(); ++i) {
        assert(vertices.vertices()[i].position.x == expectedPositions[i].x
            && vertices.vertices()[i].position.y == expectedPositions[i].y);
        assert(vertices.vertices()[i].uv.x == expectedUvs[i].x
            && vertices.vertices()[i].uv.y == expectedUvs[i].y);
    }
    const auto vertexWarm = core::defaultCpuAllocator().stats().allocationCount;
    for (int frame = 0; frame < 100; ++frame)
        assert(vertices.build(plan));
    assert(core::defaultCpuAllocator().stats().allocationCount == vertexWarm);
    scene2d::SpriteVertexDrawPlan tooSmall({1, 2, 2 * 6 * sizeof(scene2d::SpriteVertex)});
    assert(!tooSmall.build(plan) && tooSmall.vertices().empty() && tooSmall.batches().empty());
    for (const auto bad : {scene2d::SpriteVertexDrawPlanDesc{0, 3, 576},
             scene2d::SpriteVertexDrawPlanDesc{1, 3, 575},
             scene2d::SpriteVertexDrawPlanDesc{1, UINT32_MAX, UINT64_MAX}}) {
        bool rejected = false;
        try {
            scene2d::SpriteVertexDrawPlan invalid(bad);
        } catch (const core::DiagnosticError&) {
            rejected = true;
        }
        assert(rejected);
    }
    auto exactQuota = core::makeShared<core::CpuMemoryQuota>(
        18 * sizeof(scene2d::SpriteVertex) + 3 * sizeof(scene2d::SpriteVertexBatch));
    {
        core::CpuMemoryQuotaScope scope(exactQuota);
        scene2d::SpriteVertexDrawPlan charged({1, 3, 18 * sizeof(scene2d::SpriteVertex)});
        assert(exactQuota->liveBytes() == exactQuota->limitBytes() && charged.build(plan));
        const auto charge = core::inspectCpuAllocationCharge(charged.vertices().data());
        assert(charge.found && charge.quota == exactQuota.get()
            && charge.sizeBytes == 18 * sizeof(scene2d::SpriteVertex));
    }
    assert(exactQuota->liveBytes() == 0);
    auto shortQuota = core::makeShared<core::CpuMemoryQuota>(exactQuota->limitBytes() - 1);
    bool allocationRejected = false;
    try {
        core::CpuMemoryQuotaScope scope(shortQuota);
        scene2d::SpriteVertexDrawPlan charged({1, 3, 18 * sizeof(scene2d::SpriteVertex)});
    } catch (const std::bad_alloc&) {
        allocationRejected = true;
    }
    assert(allocationRejected && shortQuota->liveBytes() == 0);
    core::Rect2<float> farViewport{20, 20, 1, 1};
    assert(plan.build(manager, items, &farViewport) && plan.instances().empty()
        && plan.culledCount() == 3);
    assert(vertices.build(plan) && vertices.vertices().empty() && vertices.batches().empty());
    items[1].sprite = a;
    assert(
        plan.build(manager, items) && plan.batches().size() == 1 && plan.batches()[0].count == 3);
    for (auto& item : items) {
        item.sorting.ySort = true;
        item.sorting.worldY = float(item.sorting.objectId);
    }
    items[2].flipX = true;
    assert(plan.build(manager, items) && plan.instances()[0].parameters.x == 1);
    items[2].world.rotation = {0, 0, std::sqrt(.5f), std::sqrt(.5f)};
    assert(plan.build(manager, items));
    assert(std::abs(plan.instances()[0].axisX.y - 1) < 1e-5f);
    items[2].color = {.25f, .5f, .75f, .5f};
    items[2].flipY = true;
    assert(plan.build(manager, items) && vertices.build(plan));
    // Rotated + both flipped: compare explicit corners, not a copy of the
    // lowering formula. Straight alpha remains for the fragment stage.
    const auto& rotated = vertices.vertices();
    assert(std::abs(rotated[0].position.x - .5f) < 1e-5f
        && std::abs(rotated[0].position.y + .5f) < 1e-5f);
    assert(std::abs(rotated[2].position.x + .5f) < 1e-5f
        && std::abs(rotated[2].position.y - .5f) < 1e-5f);
    assert(rotated[0].uv.x == .5f && rotated[0].uv.y == 0 && rotated[2].uv.x == 0
        && rotated[2].uv.y == 1);
    assert(rotated[0].color.x == .25f && rotated[0].color.w == .5f);
    const auto bounds = scene2d::transformBounds(
        scene2d::Bounds2D::explicitBounds({1, 0, 0}, {2, 1, 0}), items[2].world);
    assert(std::abs(bounds.center.y - 1) < 1e-5f && std::abs(bounds.extents.x - 1) < 1e-5f
        && std::abs(bounds.extents.y - 2) < 1e-5f);
    items.push_back(items.back());
    assert(!plan.build(manager, items));
    items.pop_back();
    items[0].world.rotation.x = .5f;
    assert(!plan.build(manager, items));
    items[0].world.rotation = {};
    auto bad = manager.load<scene2d::SpriteAtlasResource>({},
        core::pmr::String("00000000-0000-4000-8000-000000000005"),
        core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/invalid.yaml"),
        core::pmr::String(ARP_SOURCE_DIR "/tests/fixtures/scene_2d/atlas.png"),
        65536u);
    assert(!bad.success && bad.diagnostics == "ARP-2D-ATLAS-SLICE-INVALID");
    assert(manager.unload(tests::spriteA).success);
    assert(!a.ready() && !plan.build(manager, items));
    assert(!vertices.build(plan) && vertices.vertices().empty() && vertices.batches().empty());
    assert(manager
            .load<scene2d::SpriteResource>({},
                core::pmr::String(tests::spriteA),
                core::pmr::String(tests::atlasA),
                core::pmr::String("left"))
            .success);
    assert(a.stale() && !a.ready());
    for (auto& item : items)
        item.sprite = manager.find<scene2d::SpriteResource>(tests::spriteA);
    assert(plan.build(manager, items) && vertices.build(plan));
    assert(manager.unload(tests::atlasA).success);
    assert(!vertices.build(plan) && vertices.vertices().empty() && vertices.batches().empty()
        && std::string_view(vertices.diagnostic()) == "ARP-2D-VERTEX-ATLAS-STALE");
    std::puts("PASS: sprite resources, ordered batching, transforms and stale generation");
}

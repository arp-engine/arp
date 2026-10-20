#include "animation/linear_blend_skinning.h"
#include "scene_content.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace arp;
using namespace arp::build_a_queen;

int main(int argc, char** argv)
{
    assert(argc >= 2);
    std::array<animation::Mat4, 2> matrices{
        animation::Mat4::identity(), animation::Mat4::translation({4, 2, -2})};
    animation::LinearBlendSkinning::Result result;
    assert(animation::LinearBlendSkinning::evaluate(
        matrices, {0, 1, 0, 0}, {.25f, .75f, 0, 0}, {1, 2, 3}, {0, 1, 0}, result));
    assert(std::abs(result.position.x - 4) < .00001f);
    assert(std::abs(result.position.y - 3.5f) < .00001f);
    assert(std::abs(result.position.z - 1.5f) < .00001f);
    assert(!animation::LinearBlendSkinning::evaluate(
        matrices, {8, 0, 0, 0}, {1, 0, 0, 0}, {}, {0, 1, 0}, result));
    assert(!animation::LinearBlendSkinning::evaluate(matrices, {}, {}, {}, {0, 1, 0}, result));
    SceneContent content;
    const auto created = content.createContent(core::pmr::String(argv[1]));
    if (!created.success) {
        std::fprintf(stderr, "%s\n", created.diagnostics.c_str());
        return 1;
    }
    assert(!content.createContent(core::pmr::String(argv[1])).success);
    std::printf("Content budget: world=%u preview=%zu stride=%zu\n",
        content.vertexCapacity(),
        content.previewVertexCapacity(),
        sizeof(SceneContent::Vertex));
    Game game;
    assert(game.start(1));
    SceneContent::PreparedFrame first, second;
    const auto prepare = [&](float seconds, SceneContent::PreparedFrame& frame) {
        return content.prepareFrame(game.snapshot(),
            game.levelDesc(),
            game.pickups(),
            game.resolvedPickups(),
            game.pickupCount(),
            seconds,
            frame);
    };
    assert(prepare(0, first) && prepare(.23f, second));
    assert(first.world.size() == second.world.size() && !first.preview.empty());
    bool moved = false;
    for (std::size_t i = 0; i < first.world.size(); ++i)
        if (std::abs(first.world[i].position[1] - second.world[i].position[1]) > .001f)
            moved = true;
    assert(moved);
    assert(!content.prepareFrame(game.snapshot(), game.levelDesc(), nullptr, nullptr, 1, 0, first));
    for (unsigned frame = 0; frame < 720; ++frame) {
        assert(game.update({}, 1.0f / 60));
        if (frame % 60 == 0)
            assert(prepare(float(frame) / 60, first));
    }
    assert(game.snapshot().phase == Phase::Result);
    assert(game.snapshot().collected > 0);
    assert(prepare(12, first));
    std::printf(
        "Queen shared content: animation, model, pickups, result and 3D preview passed; vertices=%zu preview=%zu\n",
        first.world.size(),
        first.preview.size());
}

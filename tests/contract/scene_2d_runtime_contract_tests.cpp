#include "2d/runtime_contract.h"
#include "core/runtime/game_clock.h"
#include "core/seeded_random.h"

#include <cassert>
#include <cstdio>
#include <limits>

int main()
{
    using namespace arp;
    scene2d::Viewport2DMapping view;
    assert(view.configure(1920, 1080, {1280, 720}, false));
    assert(view.scale() == 1.5f);
    core::Vec2 p;
    assert(view.screenToWorld({960, 540}, {3, 4}, 32, p) && p.x == 3 && p.y == 4);
    auto screen = view.worldToScreen({5, 7}, {3, 4}, 32);
    assert(view.screenToWorld(screen, {3, 4}, 32, p) && p.x == 5 && p.y == 7);
    assert(view.configure(1920, 1080, {1280, 720}, true) && view.scale() == 1);
    assert(!view.screenToWorld({0, 0}, {}, 32, p));
    assert(!view.configure(320, 180, {1280, 720}, true));
    assert(!view.configure(0, 1080, {1280, 720}, false));
    assert(!view.valid());
    using B = rhi::Backend;
    for (auto b : {B::Null, B::Vulkan, B::Metal, B::WebGPU}) {
        auto d = scene2d::lowerExecution2D(b, {});
        assert(!d.executable());
        assert(d.mode
            == (b == B::Null ? scene2d::Execution2D::ValidatorOnly
                             : scene2d::Execution2D::DiagnosticFailure));
    }
    core::GameClock clock(1.0 / 60, 4);
    core::FixedStepFrame frame;
    assert(clock.advance(1, frame) && frame.steps == 4 && frame.debtSeconds > .9);
    clock.setPaused(true);
    assert(clock.advance(1, frame) && frame.steps == 0 && clock.ticks() == 4);
    clock.setPaused(false);
    assert(clock.advance(0, frame) && frame.steps == 4);
    assert(!clock.advance(std::numeric_limits<double>::quiet_NaN(), frame));
    assert(!clock.setTimeScale(-1));
    core::SeededRandom a(42), b(42);
    for (int i = 0; i < 100; ++i)
        assert(a.next() == b.next());
    std::uint32_t n;
    assert(!a.bounded(0, n));
    for (int i = 0; i < 100; ++i) {
        assert(a.bounded(3, n));
        assert(n < 3);
    }
    scene2d::SpriteOrder x{0, 0, true, 5, 9}, y{0, 0, true, 4, 1};
    assert(scene2d::spriteOrderLess(x, y));
    y.worldY = 5;
    assert(scene2d::spriteOrderLess(y, x));
    std::puts(
        "PASS: 2D runtime contracts, fail-closed four-backend gates, viewport, clock and seed");
}

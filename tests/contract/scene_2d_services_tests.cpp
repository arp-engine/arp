#include "2d/sprite_animator.h"
#include "core/diagnostics/frame_time_statistics.h"
#include "core/time/game_clock.h"
#include "core/time/tween.h"
#include "tests/fixtures/scene_2d_fixture.h"

int main()
{
    using namespace arp;
    core::GameClock clock(1. / 60, 4);
    core::FixedStepFrame frame;
    assert(clock.advance(4. / 60, frame) && frame.steps == 4 && clock.ticks() == 4);
    assert(clock.returnUnexecutedSteps(3) && clock.ticks() == 1);
    assert(!clock.returnUnexecutedSteps(2));
    clock.setPaused(true);
    assert(clock.advance(1, frame) && frame.steps == 0 && clock.ticks() == 1);
    clock.setPaused(false);
    assert(clock.advance(0, frame) && frame.steps == 3 && clock.ticks() == 4);
    core::FrameTimeStatistics stats(4);
    double percentile = 0;
    assert(!stats.percentile(.95, percentile));
    assert(stats.add(4) && stats.add(1) && stats.add(3) && stats.add(2) && !stats.add(5));
    assert(stats.percentile(.5, percentile) && percentile == 2);
    assert(stats.percentile(.95, percentile) && percentile == 4);
    assert(!stats.percentile(0, percentile) && !stats.add(-1));
    core::GenerationPool<int> pool(2), foreign(2);
    std::optional<core::GenerationPool<int>> reused;
    reused.emplace(1);
    const auto obsolete = reused->emplace(1);
    reused.reset();
    reused.emplace(1);
    const auto fresh = reused->emplace(2);
    assert(fresh.owner == obsolete.owner && fresh.generation != obsolete.generation
        && !reused->get(obsolete));
    auto owner = pool.emplace(7), other = pool.emplace(8);
    assert(!pool.emplace(9) && pool.liveCount() == 2);
    assert(!foreign.get(owner));
    assert(pool.erase(other));
    auto replacement = pool.emplace(9);
    assert(replacement.index == other.index && !pool.get(other));
    core::runtime::TimerService timers(4, 4);
    auto timer = timers.schedule(owner, core::runtime::TimeDomain::Game, .25, true, 42);
    assert(timer);
    assert(timers.advance(.8, 0) && timers.events().size() == 3);
    assert(timers.advance(0, 3) && timers.events().empty());
    assert(!timers.advance(10, 0) && timers.events().empty());
    assert(timers.advance(.2, 0) && timers.events().size() == 1);
    auto ui = timers.schedule(owner, core::runtime::TimeDomain::Ui, .1, false, 9);
    assert(ui && timers.advance(0, .2) && timers.events().size() == 1
        && timers.events()[0].eventId == 9);
    timers.cancelOwner(owner);
    assert(timers.liveCount() == 0 && !timers.cancel(timer));
    core::FloatCurve curve;
    curve.keys = {{0, 0}, {1, 1}};
    core::runtime::Tween tween;
    assert(tween.start(owner, core::runtime::TimeDomain::Game, curve, 1, 10, 20));
    assert(tween.advance(.5, 0) && tween.value() == 15 && tween.active());
    assert(tween.advance(0, 10) && tween.value() == 15);
    tween.cancelOwner(owner);
    assert(!tween.active());
    resource::ResourceManager manager;
    assert(tests::loadSpriteFixture(manager));
    core::pmr::Vector<scene2d::SpriteAnimationFrame> frames;
    frames.push_back({core::pmr::String(tests::spriteA), .125, 1});
    frames.push_back({core::pmr::String(tests::spriteB), .125, 2});
    const core::pmr::String id = "00000000-0000-4000-8000-000000000010";
    assert(manager.load<scene2d::SpriteAnimationResource>({}, id, frames, true).success);
    scene2d::SpriteAnimator animator(8);
    assert(animator.play(manager.find<scene2d::SpriteAnimationResource>(id)));
    assert(animator.advance(.5) && animator.events().size() == 6 && animator.frame() == 0);
    animator.setPaused(true);
    assert(animator.advance(1) && animator.frame() == 0 && animator.events().empty());
    animator.setPaused(false);
    assert(animator.setSpeed(2));
    assert(animator.advance(.0625) && animator.frame() == 1);
    assert(!animator.advance(100) && animator.frame() == 1 && animator.events().empty());
    assert(manager.unload(id).success && !animator.advance(.1));
    assert(manager.load<scene2d::SpriteAnimationResource>({}, id, frames, false).success);
    assert(
        animator.play(manager.find<scene2d::SpriteAnimationResource>(id)) && animator.frame() == 0);
    assert(animator.setSpeed(1) && animator.advance(.5) && animator.completed()
        && animator.frame() == 1);
    assert(animator.events().size() == 2
        && animator.events().back().kind == scene2d::SpriteAnimationEventKind::Completed);
    assert(animator.advance(1) && animator.events().empty());
    animator.stop();
    assert(animator.completed() && !animator.spriteId());
    pool.clear();
    assert(pool.liveCount() == 0 && !pool.get(owner));
    std::puts(
        "PASS: bounded pools, timer debt, pause domains, tween, animation events and stale handles");
}

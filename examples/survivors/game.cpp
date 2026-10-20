#include "game.h"

#include "2d/script_value_memory.h"
#include "native_plugins.h"
#include "scriptvm/runtime/script_value_yaml.h"

#include <cfloat>
extern "C" int arp_register_survivors_v5(arp::scriptvm::AotRegistrationHostV5*);

namespace arp::survivors {
using namespace scriptvm;

GameRun::GameRun(resource::ResourceManager& resources,
    Content& content,
    Rules& rules,
    const GameConfig& config,
    std::uint64_t seed,
    audio::AudioSystem* audio)
    : world_(core::makeShared<physics2d::PhysicsWorld2D>(config.physics)),
      tiles_(resources,
          content.map,
          *world_,
          {config.maxResidentChunks, config.maxTileCells, 1000000}),
      random_(seed), factory_(core::makeShared<scene::SceneComponentFactory>()),
      extractor_(resources, config.maxDrawItems), objectBudget_(config.maxSceneObjects),
      componentBudget_(config.maxSceneComponents), drawBudget_(config.maxDrawItems),
      scene_(core::makeUnique<scene::Scene>())
{
    physicsEvents_.reserve(std::size_t(config.physics.maxEvents) * 2);
    if (!world_->valid()) {
        diagnostic_ = world_->diagnostic();
        return;
    }
    scene_->configureObjectCapacity(objectBudget_);
    scene_->configureEventCapacity(config.componentBudget.timerEvents);
    if (!scene2d::registerScene2DComponents(
            *factory_, resources, world_, audio, content.font(), config.componentBudget)) {
        diagnostic_ = "ARP-SURVIVORS-NATIVE-COMPONENT-FACTORY";
        return;
    }
    scene_->setComponentFactory([factory = factory_](auto&, const auto& name) {
        return factory->create(name);
    });
    if (!installNativePlugins(scope_, *scene_, scenes_, *world_)) {
        diagnostic_ = "ARP-SURVIVORS-PLUGIN-INSTALL";
        return;
    }
    const AotPackageDescriptorV5* package = nullptr;
    if (!rules.interpreter()) {
        AotRegistrationHostV5 host{kAotContractVersion,
            [](const AotPackageDescriptorV5* p, void* out) {
                *static_cast<const AotPackageDescriptorV5**>(out) = p;
                return true;
            },
            &package};
        if (arp_register_survivors_v5(&host) != 1 || !package) {
            diagnostic_ = "ARP-SURVIVORS-AOT-REGISTRATION";
            return;
        }
    }
    if (!module_.load(rules.source(),
            "survivors",
            "20000000-0000-4000-8000-000000009001",
            1,
            scope_.modules(),
            {"arp_scene", "arp_scene2d"},
            config.executionBudget,
            config.maxScriptInstances,
            package)
        || !module_.bind(*scene_)) {
        diagnostic_ = module_.diagnostic();
        return;
    }
    module_.session().setValueMemoryDescriptor(&scene2d::describeScriptValueMemory, nullptr);
    auto& flow = scene_->createObject("run-flow");
    state_ = flow.add_component("RunFlow");
    if (!state_.is<reflection::instance>()) {
        diagnostic_ = module_.diagnostic();
        return;
    }
    auto n = core::YamlContract::Node::deserialize(rules.configYaml());
    n["enemyHealth"] = config.enemyHealth;
    n["spawnPerSecond"] = config.spawnPerSecond;
    n["maxProjectiles"] = config.maxProjectiles;
    n["maxBoltShots"] = config.maxBoltShots;
    Value data;
    ScriptValueYaml yaml(config.executionBudget.maxContainerValues, 8);
    if (!yaml.decodeData(n, data)) {
        diagnostic_ = "ARP-SURVIVORS-CONFIG-VALUE-BUDGET";
        return;
    }
    arguments_.reserve(16);
    clockReference_ = Value::native_reference(&clock_);
    randomReference_ = Value::native_reference(&random_);
    if (!call("initialize",
            {data,
                content.spriteIds(),
                content.animationIds(),
                content.soundIds(),
                randomReference_,
                clockReference_}))
        return;
    auto result = scene_->update({0, 0, objectBudget_ + 1, componentBudget_, 0});
    if (!result.ok()) {
        diagnostic_ = result.diagnostic().message;
        return;
    }
    refresh();
}

bool GameRun::call(const char* function, std::initializer_list<Value> args)
{
    if (!valid())
        return false;
    arguments_.clear();
    arguments_.push_back(state_);
    for (const auto& a : args)
        arguments_.push_back(a);
    if (!module_.session().call(function, arguments_, result_)) {
        diagnostic_ = module_.session().diagnostic();
        return false;
    }
    return valid();
}

bool GameRun::refresh()
{
    if (!call("describe"))
        return false;
    if (!result_.is<reflection::hlist>()) {
        diagnostic_ = "ARP-SURVIVORS-SCRIPT-SNAPSHOT-TYPE";
        return false;
    }
    const auto& v = result_.cast<const reflection::hlist&>();
    if (v.size() != 23) {
        diagnostic_ = "ARP-SURVIVORS-SCRIPT-SNAPSHOT-SCHEMA";
        return false;
    }
    auto value = [&](std::size_t i) {
        return v[i].cast<double>();
    };
    for (std::size_t i = 0; i < v.size(); ++i)
        if (!std::isfinite(value(i))) {
            diagnostic_ = "ARP-SURVIVORS-SCRIPT-SNAPSHOT-NONFINITE";
            return false;
        }
    if (value(21) != 1 || value(22) != 1 || value(0) < 0 || value(0) > 5
        || value(0) != std::floor(value(0)) || value(1) < 0) {
        diagnostic_ = "ARP-SURVIVORS-SCRIPT-SNAPSHOT-VERSION-RANGE";
        return false;
    }
    for (std::size_t i = 3; i <= 16; ++i)
        if (value(i) < 0 || value(i) > INT32_MAX || value(i) != std::floor(value(i))) {
            diagnostic_ = "ARP-SURVIVORS-SCRIPT-SNAPSHOT-RANGE";
            return false;
        }
    if (value(9) > 1 || value(10) > 1) {
        diagnostic_ = "ARP-SURVIVORS-SCRIPT-SNAPSHOT-BOOL";
        return false;
    }
    telemetry_ = {RunState(int(value(0))),
        value(1),
        float(value(2)),
        std::uint32_t(value(3)),
        std::uint32_t(value(4)),
        std::uint32_t(value(5)),
        std::uint32_t(value(6)),
        std::uint32_t(value(7)),
        std::uint32_t(value(8)),
        bool(value(9)),
        bool(value(10)),
        std::uint64_t(value(11)),
        clock_.debtSeconds()};
    weapons_ = {std::uint32_t(value(12)),
        std::uint32_t(value(13)),
        std::uint32_t(value(14)),
        std::uint32_t(value(15)),
        std::uint32_t(value(16)),
        float(value(17))};
    player_ = {float(value(18)), float(value(19))};
    return true;
}

bool GameRun::update(const RunInput& in, double real, bool autoPlay)
{
    if (!valid() || !std::isfinite(real) || real < 0 || real > FLT_MAX)
        return false;
    auto* fields = state_.ptr<reflection::instance>();
    fields->slot("start") = makeBool(in.start);
    fields->slot("pause") = makeBool(in.pause);
    fields->slot("choice") = makeInt(in.upgrade);
    fields->slot("autoplay") = makeBool(autoPlay);
    fields->slot("mx") = makeFloat(in.movement.x);
    fields->slot("my") = makeFloat(in.movement.y);
    fields->slot("restart") = makeBool(in.restart);
    fields->slot("volume_delta") = makeInt(in.volumeDelta);
    auto checked = [&](const scene::SceneUpdateResult& result) {
        if (result.ok())
            return true;
        diagnostic_ = result.diagnostic().message;
        return false;
    };
    if (!checked(scene_->update({clock_.paused() ? 0 : float(real),
            clock_.ticks(),
            objectBudget_ + 1,
            componentBudget_,
            float(real)}))
        || !refresh())
        return false;
    core::FixedStepFrame frame;
    if (!clock_.advance(real, frame)) {
        diagnostic_ = "ARP-SURVIVORS-CLOCK";
        return false;
    }
    interpolation_ = float(frame.interpolation);
    for (std::uint32_t i = 0; i < frame.steps; ++i) {
        if (!tiles_.updateResidency({player_.x - 16, player_.y - 16, 32, 32})) {
            diagnostic_ = tiles_.diagnostic();
            return false;
        }
        for (const auto& object : scene_->objects())
            if (auto* body = object->getComponent<physics2d::RigidBody2DComponent>())
                body->beforeStep();
        if (!checked(scene_->fixedUpdate({1.f / 60,
                clock_.ticks() - frame.steps + i + 1,
                objectBudget_ + 1,
                componentBudget_})))
            return false;
        if (!world_->step()) {
            diagnostic_ = world_->diagnostic();
            return false;
        }
        for (const auto& object : scene_->objects())
            if (auto* body = object->getComponent<physics2d::RigidBody2DComponent>()) {
                if (!body->afterStep() || !body->synchronize(*scene_, 1)) {
                    diagnostic_ = "ARP-SURVIVORS-BODY-SYNCHRONIZE";
                    return false;
                }
            }
        physicsEvents_.clear();
        for (const auto& event : world_->events()) {
            const char* hook = nullptr;
            switch (event.type) {
            case physics2d::EventType::ContactBegin:
                hook = "on_collision_enter";
                break;
            case physics2d::EventType::ContactEnd:
                hook = "on_collision_exit";
                break;
            case physics2d::EventType::SensorBegin:
                hook = "on_trigger_enter";
                break;
            case physics2d::EventType::SensorEnd:
                hook = "on_trigger_exit";
                break;
            }
            std::uint64_t objectA = 0, objectB = 0;
            if (hook == nullptr || !world_->objectId(event.a, objectA)
                || !world_->objectId(event.b, objectB)) {
                diagnostic_ = world_->diagnostic();
                return false;
            }
            const scene::GameObjectId a = scene::GameObjectId::fromValue(objectA);
            const scene::GameObjectId b = scene::GameObjectId::fromValue(objectB);
            if (scene_->findObject(a) == nullptr || scene_->findObject(b) == nullptr) {
                diagnostic_ = "ARP-SURVIVORS-PHYSICS-EVENT-OBJECT";
                return false;
            }
            physicsEvents_.push_back({a, b, hook});
            physicsEvents_.push_back({b, a, hook});
        }
        if (!physicsEvents_.empty()
            && !checked(scene_->dispatchPhysicsEvents(
                physicsEvents_, {0, clock_.ticks(), objectBudget_ + 1, componentBudget_})))
            return false;
        if (!checked(
                scene_->dispatchEvents({0, clock_.ticks(), objectBudget_ + 1, componentBudget_}))
            || !refresh())
            return false;
        if (clock_.paused()) {
            if (!clock_.returnUnexecutedSteps(frame.steps - i - 1)) {
                diagnostic_ = "ARP-SURVIVORS-CLOCK-RETURN";
                return false;
            }
            interpolation_ = 1;
            break;
        }
    }
    return hostUpdate(in);
}

bool GameRun::extract(core::Vec2 camera, float interpolation)
{
    if (!valid() || !std::isfinite(interpolation) || interpolation < 0 || interpolation > 1)
        return false;
    for (const auto& object : scene_->objects()) {
        if (auto* body = object->getComponent<physics2d::RigidBody2DComponent>())
            if (!body->synchronize(*scene_, interpolation)) {
                diagnostic_ = "ARP-SURVIVORS-EXTRACT-BODY";
                return false;
            }
        if (auto* canvas = object->getComponent<scene2d::ScreenCanvas2DComponent>())
            if (!canvas->applyCamera(camera)) {
                diagnostic_ = "ARP-SURVIVORS-CANVAS";
                return false;
            }
    }
    if (!tiles_.updateResidency({camera.x - 16, camera.y - 16, 32, 32})
        || !tiles_.extract(telemetry_.seconds)) {
        diagnostic_ = tiles_.diagnostic();
        return false;
    }
    auto snapshot = scene_->extractTraversalSnapshot({objectBudget_ + 1});
    if (!extractor_.extract(*scene_, snapshot)) {
        diagnostic_ = extractor_.diagnostic();
        return false;
    }
    items_.clear();
    if (tiles_.items().size() + extractor_.items().size() > drawBudget_) {
        diagnostic_ = "ARP-SURVIVORS-DRAW-CAPACITY";
        return false;
    }
    items_.reserve(drawBudget_);
    items_.insert(items_.end(), tiles_.items().begin(), tiles_.items().end());
    items_.insert(items_.end(), extractor_.items().begin(), extractor_.items().end());
    return true;
}

bool GameRun::populateStress()
{
    return call("populate_stress") && refresh();
}

void GameRun::forceDefeat()
{
    call("force_defeat");
    refresh();
}

bool GameRun::grantExperience(std::uint32_t amount)
{
    return amount <= INT32_MAX && call("grant_experience", {makeInt(int(amount))}) && refresh();
}

bool GameRun::setRecord(const Record& r)
{
    if (r.bestKills > INT32_MAX || r.bestSeconds > INT32_MAX || r.wins > INT32_MAX
        || r.unlockPoints > INT32_MAX) {
        diagnostic_ = "ARP-SURVIVORS-RECORD-SCRIPT-RANGE";
        return false;
    }
    record_ = r;
    return call("set_record",
        {makeInt(int(r.bestKills)),
            makeInt(int(r.bestSeconds)),
            makeInt(int(r.wins)),
            makeInt(int(r.unlockPoints)),
            makeFloat(r.volume)});
}

bool GameRun::hostUpdate(const RunInput& in)
{
    if (!call("host_snapshot"))
        return false;
    if (!result_.is<reflection::hlist>()) {
        diagnostic_ = "ARP-SURVIVORS-HOST-SNAPSHOT-TYPE";
        return false;
    }
    const auto& v = result_.cast<const reflection::hlist&>();
    if (v.size() != 8 || v[0].cast<double>() != 1) {
        diagnostic_ = "ARP-SURVIVORS-HOST-SNAPSHOT-VERSION";
        return false;
    }
    for (std::size_t i = 1; i < 7; ++i) {
        double n = v[i].cast<double>();
        if (!std::isfinite(n) || n < 0 || n > INT32_MAX || n != std::floor(n)) {
            diagnostic_ = "ARP-SURVIVORS-HOST-SNAPSHOT-RANGE";
            return false;
        }
    }
    auto volume = v[7].cast<double>();
    if (!std::isfinite(volume) || volume < 0 || volume > 1) {
        diagnostic_ = "ARP-SURVIVORS-HOST-VOLUME";
        return false;
    }
    recordRevision_ = v[1].cast<double>();
    restartRequested_ = v[2].cast<double>() != 0;
    record_ = {std::uint32_t(v[3].cast<double>()),
        std::uint32_t(v[4].cast<double>()),
        std::uint32_t(v[5].cast<double>()),
        std::uint32_t(v[6].cast<double>()),
        volume};
    return true;
}
} // namespace arp::survivors

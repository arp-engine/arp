#include "2d/camera_2d.h"
#include "core/frame_time_statistics.h"
#include "game.h"
#include "platform/render_system.h"
#include "platform/renderdoc_capture_scope.h"
#include "records.h"
#include "renderer/scene_2d_pipeline.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace {
int nullSmoke(std::uint64_t seed)
{
    auto device = arp::rhi::createNullDevice();
    if (!device)
        return 1;
    const auto report = arp::rhi::NullHiddenFrameSmokeRunner(*device).run(
        {"SurvivorsP0", {1280, 720}, arp::rhi::TextureFormat::Bgra8Unorm, 2});
    if (!report.success) {
        std::fprintf(stderr, "%s\n", report.diagnostics.c_str());
        return 1;
    }
    arp::core::GameClock clock(1.0 / 60, 4);
    arp::core::FixedStepFrame frame;
    arp::core::SeededRandom random(seed);
    std::uint32_t checksum = 0;
    for (int i = 0; i < 120; ++i) {
        if (!clock.advance(1.0 / 60, frame))
            return 1;
        checksum ^= random.next();
    }
    std::printf(
        "type: arp.survivors.smoke\ncontractVersion: 1\nstatus: ContractComplete\nbackend: NullValidatorOnly\nseed: %llu\nticks: %llu\nchecksum: %u\nsubmittedFrames: %u\n",
        static_cast<unsigned long long>(seed),
        static_cast<unsigned long long>(clock.ticks()),
        checksum,
        report.submittedFrameCount);
    return report.submittedFrameCount == 2 && clock.ticks() == 120 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    using namespace survivors;
    using Clock = std::chrono::steady_clock;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool null = false, hidden = false, autoplay = false, fixed = false, simulate = false,
         stress = false, integer = false, mute = false, resizeCheck = false,
         scriptInterpreter = true;
    std::uint64_t seed = 42, framesLimit = 0, captureFrame = UINT64_MAX, restarts = 0,
                  normalRestarts = 0, endedFrame = UINT64_MAX;
    std::uint32_t width = 1280, height = 720, resultHold = 0, startDelay = 0, captureEveryMs = 1000;
    double secondsLimit = 0, endedAt = -1;
    core::pmr::String captureSeries;
    std::uint32_t seriesCount = 0;
    const auto executable = std::filesystem::absolute(std::filesystem::path(argv[0])).parent_path();
    core::pmr::String root((executable / "assets").string().c_str()),
        saveRoot((executable / "user-data").string().c_str()), capturePath, captureState, scenario;
    auto integerArgument = [&](int& i, auto& value) {
        if (++i >= argc)
            return false;
        const std::string_view input(argv[i]);
        auto result = std::from_chars(input.data(), input.data() + input.size(), value);
        return result.ec == std::errc{} && result.ptr == input.data() + input.size();
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        bool valid = true;
        if (arg == "--script-interpreter")
            scriptInterpreter = true;
        else if (arg == "--script-aot")
            scriptInterpreter = false;
        else if (arg == "--null-smoke")
            null = true;
        else if (arg == "--hidden")
            hidden = true;
        else if (arg == "--autoplay")
            autoplay = true;
        else if (arg == "--fixed-step")
            fixed = true;
        else if (arg == "--simulate")
            simulate = true;
        else if (arg == "--stress")
            stress = true;
        else if (arg == "--resize-check")
            resizeCheck = true;
        else if (arg == "--integer-scale")
            integer = true;
        else if (arg == "--mute")
            mute = true;
        else if (arg == "--seed")
            valid = integerArgument(i, seed);
        else if (arg == "--frames")
            valid = integerArgument(i, framesLimit);
        else if (arg == "--width")
            valid = integerArgument(i, width);
        else if (arg == "--height")
            valid = integerArgument(i, height);
        else if (arg == "--seconds") {
            std::uint32_t seconds = 0;
            valid = integerArgument(i, seconds);
            secondsLimit = seconds;
        } else if (arg == "--capture-frame")
            valid = integerArgument(i, captureFrame);
        else if (arg == "--restart-check")
            valid = integerArgument(i, restarts);
        else if (arg == "--normal-restart-check")
            valid = integerArgument(i, normalRestarts) && normalRestarts >= 3;
        else if (arg == "--result-hold")
            valid = integerArgument(i, resultHold);
        else if (arg == "--start-delay")
            valid = integerArgument(i, startDelay);
        else if (arg == "--capture-every-ms")
            valid = integerArgument(i, captureEveryMs);
        else if (arg == "--capture-series" && i + 1 < argc)
            captureSeries = argv[++i];
        else if (arg == "--assets" && i + 1 < argc)
            root = argv[++i];
        else if (arg == "--save-dir" && i + 1 < argc)
            saveRoot = argv[++i];
        else if (arg == "--capture-path" && i + 1 < argc)
            capturePath = argv[++i];
        else if (arg == "--capture-state" && i + 1 < argc)
            captureState = argv[++i];
        else if (arg == "--scenario" && i + 1 < argc)
            scenario = argv[++i];
        else
            valid = false;
        if (!valid) {
            std::fprintf(stderr, "ARP-SURVIVORS-ARGUMENT: %s\n", argv[i]);
            return 2;
        }
    }
    if (null)
        return nullSmoke(seed);
    if (!scenario.empty() && scenario != "death" && scenario != "pause" && scenario != "upgrades"
        && scenario != "evolution")
        return 2;
    if (!captureEveryMs || !width || !height || width > INT32_MAX || height > INT32_MAX
        || ((captureFrame != UINT64_MAX || !captureState.empty()) && capturePath.empty()))
        return 2;
    if (stress && (fixed || simulate)) {
        std::fputs("ARP-SURVIVORS-STRESS-REQUIRES-REAL-TIME-NATIVE-RENDERING\n", stderr);
        return 2;
    }
    if (normalRestarts
        && (stress || restarts || framesLimit || secondsLimit || !scenario.empty())) {
        std::fputs("ARP-SURVIVORS-NORMAL-RESTART-ARGUMENT-CONFLICT\n", stderr);
        return 2;
    }
    GameConfig config;
    core::pmr::String diagnostic;
    Rules rules;
    if (!config.load(root, diagnostic)
        || !rules.load(root, config.maxSourceBytes, diagnostic)) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 1;
    }
    rules.useInterpreter(scriptInterpreter);
    std::printf("scriptExecution: %s\n", scriptInterpreter ? "Interpreter" : "DirectNativeV5");
    resource::ResourceManager resources;
    Content content(resources);
    if (!content.load(root, config, diagnostic)) {
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
        return 1;
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(std::filesystem::path(saveRoot.c_str()), filesystemError);
    if (filesystemError) {
        std::fprintf(
            stderr, "ARP-SURVIVORS-SAVE-DIRECTORY: %s\n", filesystemError.message().c_str());
        return 1;
    }
    core::io::AtomicFileService files(2, 65536);
    core::io::VersionedYamlStore<RecordSchema> store(files);
    Record record;
    const auto savePath = saveRoot + "/record.yaml";
    auto load = store.load(savePath);
    if (!load)
        return 1;
    core::io::AtomicFileResult loaded;
    const auto loadDeadline = Clock::now() + std::chrono::seconds(10);
    while (!store.take(load, loaded)) {
        if (Clock::now() > loadDeadline)
            return 1;
        std::this_thread::yield();
    }
    if (loaded.success)
        record = RecordSchema::decode(loaded.bytes);
    else if (!loaded.missing) {
        std::fprintf(stderr, "%s (original retained)\n", loaded.diagnostic.c_str());
        return 1;
    }
    core::UniquePtr<audio::AudioSystem> audio;
    if (!mute) {
        audio = core::makeUnique<audio::AudioSystem>();
        if (audio->status() != audio::AudioStatus::Ready) {
            std::fputs(
                "ARP-SURVIVORS-AUDIO-DEVICE-UNAVAILABLE; use explicit --mute for silent automation\n",
                stderr);
            return 1;
        }
    }
    auto game = core::makeUnique<GameRun>(resources, content, rules, config, seed, audio.get());
    if (!game->setRecord(record)) {
        std::fprintf(stderr, "%s\n", game->diagnostic().c_str());
        return 1;
    }
    int savedRevision = 0;
    if (!game->valid()) {
        std::fprintf(stderr, "%s\n", game->diagnostic().c_str());
        return 1;
    }
    const auto restartBaseline = game->physics().telemetry();
    const auto restartObjects = game->liveCounts();
    std::uint64_t completedRestarts = 0, restartCpuBytes = 0, restartThirdPartyBytes = 0,
                  restartGpuBytes = 0;
    if (restarts) {
        fixed = true;
        if (!game->populateStress())
            return 1;
    } else if (stress && !game->populateStress()) {
        std::fprintf(stderr, "%s\n", game->diagnostic().c_str());
        return 1;
    }
    if (normalRestarts) {
        fixed = true;
        restarts = normalRestarts;
    }
    platform::WindowSystem windowSystem;
    platform::PlatformWindow window;
    platform::RenderSystem renderSystem;
    core::UniquePtr<renderer::Scene2DRenderPipeline> pipeline;
    if (!simulate) {
        if (!windowSystem.initialize().success)
            return 1;
        window = windowSystem.createWindow(
            {"Copperlight Survivors", int(width), int(height), hidden, true});
        if (!window.valid())
            return 1;
        renderSystem = platform::createDefaultRenderSystem(
            windowSystem, window, "Copperlight Survivors", rhi::PresentationMode::Immediate);
        if (!renderSystem.valid() || renderSystem.backend() != rhi::Backend::Vulkan) {
            std::fprintf(stderr, "%s\n", renderSystem.diagnostics().c_str());
            return 1;
        }
        renderer::Scene2DRendererDesc description;
        description.shaderPath =
            core::pmr::String((executable / "shaders/scene_2d_sprites.slang").string().c_str());
        description.colorFormat = renderSystem.colorFormat();
        description.maxInstances = config.maxDrawItems;
        description.maxAtlases = 1;
        description.frameSlots = 3;
        pipeline =
            core::makeUnique<renderer::Scene2DRenderPipeline>(resources, std::move(description));
        auto result = pipeline->create({*renderSystem.gpuDevice(),
            renderSystem.colorFormat(),
            rhi::TextureFormat::Unknown,
            {width, height}});
        if (result.success)
            result = pipeline->prepareAtlas(content.atlas);
        if (!result.success) {
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
            return 1;
        }
    }
    scene2d::Viewport2DMapping mapping;
    scene2d::Camera2D camera;
    scene2d::SpriteDrawPlan plan(config.maxDrawItems);
    platform::WindowEventBatch events;
    events.windowEvents.reserve(64);
    events.inputEvents.reserve(256);
    std::array<bool, static_cast<std::size_t>(platform::WindowKey::Count)> held{};
    platform::RenderDocCaptureScope capture;
    bool captured = false, close = false, scenarioStarted = false, scenarioResumed = false,
         evolutionMenu = false;
    double pauseStarted = 0;
    std::uint32_t resizeEvents = 0;
    core::PoolHandle saving;
    core::io::AtomicFileResult saved;
    bool saveDirty = false;
    auto save = [&]() {
        saveDirty = true;
        return true;
    };
    auto flushSave = [&]() {
        if (saving || !saveDirty)
            return true;
        saving = store.save(savePath, RecordSchema::encode(record));
        if (!saving)
            return false;
        saveDirty = false;
        return true;
    };
    auto previous = Clock::now(), started = previous, lastPresented = previous;
    std::uint64_t frames = 0, deferredFrames = 0;
    double deferredSince = -1;
    core::FrameTimeStatistics frameTimes(config.maxFrameSamples), cpuTimes(config.maxFrameSamples),
        gpuTimes(config.maxFrameSamples);
    std::uint64_t lastGpuSerial = 0;
    std::uint32_t maximumDraws = 0, maximumInstances = 0;
    const auto memoryBaseline = core::defaultCpuAllocator().stats();
    auto sampleBaseline = memoryBaseline;
    bool sampling = false;
    std::uint64_t sampledThirdPartyBytes = 0, sampledThirdPartyPeakBytes = 0;
    if (stress && secondsLimit == 0)
        secondsLimit = config.warmup + config.sample;
    if (simulate)
        fixed = true;
    std::puts("ready: true");
    while (!close) {
        const auto begin = Clock::now();
        const double elapsed = std::chrono::duration<double>(begin - started).count();
        if (stress && !sampling && elapsed >= config.warmup) {
            sampling = true;
            sampleBaseline = core::defaultCpuAllocator().stats();
            const auto p = game->physics().telemetry();
            sampledThirdPartyBytes = p.thirdPartyBytes;
            sampledThirdPartyPeakBytes = p.thirdPartyPeakBytes;
        }
        const double delta =
            fixed ? 1.0 / 60 : std::chrono::duration<double>(begin - previous).count();
        previous = begin;
        if (saving && store.take(saving, saved)) {
            saving = {};
            if (!saved.success) {
                std::fprintf(stderr, "%s\n", saved.diagnostic.c_str());
                return 1;
            }
        }
        if (!flushSave())
            return 1;
        RunInput input;
        if (!scenarioStarted && game->telemetry().seconds >= 1) {
            scenarioStarted = true;
            if (scenario == "death")
                game->forceDefeat();
            if (scenario == "pause") {
                input.pause = true;
                pauseStarted = elapsed;
            }
            if (scenario == "upgrades" && !game->grantExperience(config.xpPerUpgrade * 3))
                return 1;
            if (scenario == "evolution" && !game->grantExperience(config.xpPerUpgrade * 18))
                return 1;
        }
        if (scenario == "evolution" && !evolutionMenu && game->telemetry().seconds >= 12) {
            // Autoplay consumes one selection per frame; two queued choices
            // leave a real upgrade frame for the evolved-menu capture.
            evolutionMenu = true;
            if (!game->grantExperience(config.xpPerUpgrade * 2))
                return 1;
        }
        if (scenario == "pause" && scenarioStarted && !scenarioResumed
            && elapsed - pauseStarted >= 1) {
            input.pause = true;
            scenarioResumed = true;
        }
        if (!simulate) {
            if (resizeCheck && (frames == 20 || frames == 40 || frames == 60)) {
                window.resize(frames == 20 ? 1920
                        : frames == 40     ? 1600
                                           : 1280,
                    frames == 20       ? 1080
                        : frames == 40 ? 1000
                                       : 720);
                if (!window.sync().success)
                    return 1;
            }
            events.clear();
            windowSystem.pumpEvents(events);
            for (const auto& event : events.windowEvents) {
                if (event.type == platform::WindowEventType::CloseRequested)
                    close = true;
                if (event.type == platform::WindowEventType::Resized && window.pixelWidth() > 0
                    && window.pixelHeight() > 0) {
                    ++resizeEvents;
                    width = window.pixelWidth();
                    height = window.pixelHeight();
                    auto result = renderSystem.resize({width, height});
                    if (!result.success)
                        return 1;
                    pipeline->resize({width, height});
                }
            }
            for (const auto& event : events.inputEvents) {
                if (event.kind == platform::WindowInputEvent::Kind::FocusLost) {
                    held.fill(false);
                    continue;
                }
                const auto key = std::size_t(event.key);
                if (key >= held.size())
                    continue;
                const bool down = event.kind == platform::WindowInputEvent::Kind::KeyDown;
                if (event.kind != platform::WindowInputEvent::Kind::KeyUp && !down)
                    continue;
                const bool pressed = down && !held[key];
                held[key] = down;
                if (!pressed)
                    continue;
                input.start |= event.key == platform::WindowKey::Enter;
                input.pause |= event.key == platform::WindowKey::Escape;
                if (event.key >= platform::WindowKey::Num1
                    && event.key <= platform::WindowKey::Num3)
                    input.upgrade = int(event.key) - int(platform::WindowKey::Num1);
                input.restart |= event.key == platform::WindowKey::R;
                if (event.key == platform::WindowKey::Minus)
                    --input.volumeDelta;
                if (event.key == platform::WindowKey::Equals)
                    ++input.volumeDelta;
            }
            auto key = [&](platform::WindowKey k) {
                return held[std::size_t(k)];
            };
            input.movement = {
                float(key(platform::WindowKey::D) || key(platform::WindowKey::ArrowRight))
                    - float(key(platform::WindowKey::A) || key(platform::WindowKey::ArrowLeft)),
                float(key(platform::WindowKey::W) || key(platform::WindowKey::ArrowUp))
                    - float(key(platform::WindowKey::S) || key(platform::WindowKey::ArrowDown))};
        }
        if (!game->update(
                input, delta, normalRestarts || (autoplay && !stress && elapsed >= startDelay))) {
            std::fprintf(stderr, "%s\n", game->diagnostic().c_str());
            return 1;
        }
        const auto t = game->telemetry();
        const bool ended = t.state == RunState::Victory || t.state == RunState::Defeat;
        if (ended && endedAt < 0) {
            endedAt = elapsed;
            endedFrame = frames;
        }
        if (game->recordRevision() != savedRevision) {
            record = game->record();
            savedRevision = game->recordRevision();
            if (!save())
                return 1;
        }
        if (game->restartRequested()) {
            game.reset();
            game = core::makeUnique<GameRun>(resources, content, rules, config, seed, audio.get());
            if (!game->valid() || !game->setRecord(record))
                return 1;
            savedRevision = 0;
            endedAt = -1;
            endedFrame = UINT64_MAX;
            camera.setPosition({});
        }
        if (!simulate) {
            if (!camera.follow(game->playerPosition(), float(delta), 8)
                || !mapping.configure(width, height, {1280, 720}, integer))
                return 1;
            core::Vec2 aligned;
            if (!camera.alignedPosition(config.ppu, mapping.scale(), aligned))
                return 1;
            if (!game->extract(aligned, game->interpolation())
                || !plan.build(resources, game->items())) {
                std::fprintf(stderr,
                    "draw: %s %s\n",
                    game->diagnostic().c_str(),
                    plan.diagnostic() ? plan.diagnostic() : "");
                return 1;
            }
            // Keep pumping input/simulation while a frame slot is in flight.
            // No hostWait or busy retry of render()/acquire in the hot path.
            if (!pipeline->readyForFrame(frames)) {
                ++deferredFrames;
                if (deferredSince < 0)
                    deferredSince = elapsed;
                if (elapsed - deferredSince > 10) {
                    std::fputs("ARP-SURVIVORS-GPU-COMPLETION-TIMEOUT\n", stderr);
                    return 1;
                }
                std::this_thread::yield();
                continue;
            }
            deferredSince = -1;
            const bool evolvedPulse = captureState == "evolved"
                && std::any_of(game->items().begin(), game->items().end(), [&](const auto& item) {
                       return item.sprite.id() == content.sprite("pulse").id() && item.color.w > .3f
                           && item.color.w < .6f;
                   });
            const bool captureNow = !captured
                && (frames == captureFrame
                    || (!captureState.empty()
                        && ((captureState == "upgrade" && t.state == RunState::Upgrade)
                            || (captureState == "pause" && t.state == RunState::Paused)
                            || (captureState == "victory" && t.state == RunState::Victory
                                && frames > endedFrame)
                            || (captureState == "death" && t.state == RunState::Defeat)
                            || (captureState == "combat" && t.seconds >= 60)
                            || (captureState == "physics" && frames >= 10)
                            || (captureState == "evolved" && t.upgrades >= 18 && t.seconds >= 10
                                && evolvedPulse)
                            || (captureState == "evolved-upgrade" && t.upgrades >= 18
                                && t.state == RunState::Upgrade))));
            const bool seriesNow =
                !captureSeries.empty() && elapsed >= double(seriesCount) * captureEveryMs / 1000.0;
            core::pmr::String framePath = capturePath;
            if (seriesNow) {
                char index[24];
                std::snprintf(index, sizeof(index), "-%05u", seriesCount);
                framePath = captureSeries + index;
            }
            if ((captureNow || seriesNow) && !capture.begin(framePath)) {
                std::fputs("ARP-SURVIVORS-RENDERDOC-NOT-INJECTED\n", stderr);
                return 1;
            }
            auto frame = renderSystem.swapchain()->acquireNextFrame();
            if (!frame.success)
                return 1;
            auto image = renderSystem.swapchain()->acquiredImage(frame);
            if (!image.success || !image.texture)
                return 1;
            renderer::Scene2DFramePayload payload{&plan, mapping, aligned, config.ppu};
            renderer::RenderFrameRequest request;
            request.view.frameIndex = frames;
            request.pipelinePayload = &payload;
            request.pipelinePayloadContractName = renderer::Scene2DFramePayload::contract;
            request.pipelinePayloadContractVersion = renderer::Scene2DFramePayload::version;
            renderer::RenderPipelineContext context{
                frame, {}, nullptr, image.texture.get(), nullptr, request};
            auto result = pipeline->render(context);
            if (!result.success) {
                std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
                return 1;
            }
            if (!renderSystem.swapchain()->present(frame).success)
                return 1;
            maximumDraws = std::max(maximumDraws, pipeline->lastDrawCount());
            maximumInstances = std::max(maximumInstances, std::uint32_t(plan.instances().size()));
            if (stress && elapsed >= config.warmup && elapsed < config.warmup + config.sample) {
                const auto gpu = renderSystem.gpuDevice()->latestGpuTiming();
                if (gpu.availability == rhi::GpuTimingAvailability::Available
                    && gpu.completedFrameSerial != lastGpuSerial) {
                    lastGpuSerial = gpu.completedFrameSerial;
                    for (std::uint32_t i = 0; i < gpu.scopeCount; ++i)
                        if (gpu.scopes[i].scopeId == 0 && !gpuTimes.add(gpu.scopes[i].elapsedMs))
                            return 1;
                }
            }
            if (captureNow || seriesNow) {
                renderSystem.waitGpuIdle();
                if (!capture.end())
                    return 1;
                if (captureNow)
                    captured = true;
                if (seriesNow)
                    ++seriesCount;
                std::printf("captured frame %llu game %.2fs\n",
                    static_cast<unsigned long long>(frames),
                    t.seconds);
            }
        }
        const auto completed = Clock::now();
        const double milliseconds =
            std::chrono::duration<double, std::milli>(completed - begin).count();
        const double presentationMs =
            std::chrono::duration<double, std::milli>(completed - lastPresented).count();
        lastPresented = completed;
        if (stress && elapsed >= config.warmup && elapsed < config.warmup + config.sample) {
            if (!frameTimes.add(presentationMs) || !cpuTimes.add(milliseconds)) {
                std::fputs("ARP-SURVIVORS-SAMPLE-CAPACITY\n", stderr);
                return 1;
            }
        }
        ++frames;
        // Normal memory regression runs three seconds of ordinary combat per
        // world. The legacy pressure-population check retains its own cadence.
        const std::uint64_t restartFrames = normalRestarts ? 180 : 2;
        if (restarts && frames % restartFrames == 0) {
            if (normalRestarts) {
                if (t.seconds < 2.99 || (!t.enemies && !t.kills)) {
                    std::fputs("ARP-SURVIVORS-NORMAL-RESTART-WORKLOAD-MISSING\n", stderr);
                    return 1;
                }
                std::printf(
                    "normalCombat: { iteration: %llu, seconds: %.3f, enemies: %u, projectiles: %u, pickups: %u, kills: %u }\n",
                    static_cast<unsigned long long>(completedRestarts + 1),
                    t.seconds,
                    t.enemies,
                    t.projectiles,
                    t.pickups,
                    t.kills);
                // Cold test checkpoint: drain real submissions before comparing
                // retained GPU bytes; no change to the rendering hot path.
                if (!simulate)
                    renderSystem.waitGpuIdle();
            }
            game.reset();
            game = core::makeUnique<GameRun>(resources, content, rules, config, seed, audio.get());
            if (!game->valid() || !game->setRecord(record))
                return 1;
            savedRevision = 0;
            const auto t = game->telemetry();
            const auto p = game->physics().telemetry();
            if (t.enemies || t.projectiles || t.pickups || p.bodies != restartBaseline.bodies
                || p.colliders != restartBaseline.colliders || p.joints != restartBaseline.joints
                || !(game->liveCounts() == restartObjects)) {
                std::fprintf(stderr,
                    "ARP-SURVIVORS-RESTART-BASELINE: %llu\n",
                    static_cast<unsigned long long>(completedRestarts));
                return 1;
            }
            ++completedRestarts;
            const auto live = core::defaultCpuAllocator().stats().liveBytes;
            const auto gpuBytes =
                simulate ? 0 : renderSystem.gpuDevice()->memoryStats().allocatedBytes;
            if (completedRestarts == 2) {
                restartCpuBytes = live;
                restartThirdPartyBytes = p.thirdPartyBytes;
                restartGpuBytes = gpuBytes;
            }
            if (completedRestarts > 2)
                std::printf(
                    "restartMemory: { iteration: %llu, coreDelta: %lld, box2dDelta: %lld, gpuDelta: %lld }\n",
                    static_cast<unsigned long long>(completedRestarts),
                    static_cast<long long>(live) - static_cast<long long>(restartCpuBytes),
                    static_cast<long long>(p.thirdPartyBytes)
                        - static_cast<long long>(restartThirdPartyBytes),
                    static_cast<long long>(gpuBytes) - static_cast<long long>(restartGpuBytes));
            if (normalRestarts && completedRestarts > 2
                && (live != restartCpuBytes || p.thirdPartyBytes != restartThirdPartyBytes
                    || gpuBytes != restartGpuBytes)) {
                std::fputs("ARP-SURVIVORS-NORMAL-RESTART-MEMORY-BASELINE\n", stderr);
                return 1;
            }
            if (completedRestarts == restarts)
                close = true;
            else if (!normalRestarts && !game->populateStress())
                return 1;
        }
        // A fixed step can end the run after this frame's variable HUD update.
        // Present one subsequent update before automatic exit/capture.
        if ((framesLimit && frames >= framesLimit)
            || (secondsLimit > 0 && (simulate ? t.seconds : elapsed) >= secondsLimit)
            || (autoplay && ended && frames > endedFrame + 1 && elapsed - endedAt >= resultHold))
            close = true;
    }
    if (!simulate)
        renderSystem.waitGpuIdle();
    std::printf("deferredFramePolls: %llu\n", static_cast<unsigned long long>(deferredFrames));
    std::printf("seriesCaptures: %u\n", seriesCount);
    if (resizeCheck && resizeEvents < 3) {
        std::fputs("ARP-SURVIVORS-RESIZE-EVENT-MISSING\n", stderr);
        return 1;
    }
    std::printf("resizeEvents: %u\npixelDensity: %.2f\n",
        resizeEvents,
        simulate ? 1.f : window.pixelDensity());
    std::printf("restartChecks: %llu\n", static_cast<unsigned long long>(completedRestarts));
    if (normalRestarts) {
        if (completedRestarts != normalRestarts) {
            std::fputs("ARP-SURVIVORS-NORMAL-RESTART-INCOMPLETE\n", stderr);
            return 1;
        }
        std::printf("normalRestartMemory: Passed\nnormalFramesPerRun: 180\n");
    }
    const auto final = game->telemetry();
    const auto physics = game->physics().telemetry();
    const auto memory = core::defaultCpuAllocator().stats();
    const auto live = game->liveCounts();
    std::printf("sceneObjects: %zu\nsceneComponents: %zu\nscriptInstances: %zu\n",
        live.objects,
        live.components,
        live.scripts);
    const auto& weapons = game->weapons();
    std::printf(
        "weaponEvolution: { boltShots: %u, pierces: %u, bounces: %u, blades: %u, pulseRadiusPercent: %u }\n",
        weapons.boltShots,
        weapons.boltPierces,
        weapons.boltBounces,
        weapons.orbitBlades,
        weapons.pulseRadiusPercent);
    std::printf("catchupDebtSeconds: %.6f\nrejectedSpawns: %llu\n",
        final.catchupDebtSeconds,
        static_cast<unsigned long long>(final.rejectedSpawns));
    double p95 = 0, p99 = 0, cpu95 = 0;
    frameTimes.percentile(.95, p95);
    frameTimes.percentile(.99, p99);
    cpuTimes.percentile(.95, cpu95);
    double gpu95 = 0;
    gpuTimes.percentile(.95, gpu95);
    std::printf("gpuSamples: %zu\ngpuP95Ms: %.6f\nmaxDraws: %u\nmaxInstances: %u\n",
        gpuTimes.count(),
        gpu95,
        maximumDraws,
        maximumInstances);
    std::printf(
        "sampleAllocations: %llu\nsampleLiveByteDelta: %lld\nthirdPartySampleLiveByteDelta: %lld\nthirdPartySamplePeakByteDelta: %lld\n",
        static_cast<unsigned long long>(memory.allocationCount - sampleBaseline.allocationCount),
        static_cast<long long>(memory.liveBytes) - static_cast<long long>(sampleBaseline.liveBytes),
        static_cast<long long>(physics.thirdPartyBytes)
            - static_cast<long long>(sampledThirdPartyBytes),
        static_cast<long long>(physics.thirdPartyPeakBytes)
            - static_cast<long long>(sampledThirdPartyPeakBytes));
    if (!simulate) {
        const auto gpuMemory = renderSystem.gpuDevice()->memoryStats();
        std::printf(
            "gpuAllocatedBytes: %llu\n", static_cast<unsigned long long>(gpuMemory.allocatedBytes));
    }
    std::printf(
        "type: arp.survivors-run\ncontractVersion: 1\nmode: %s\nframes: %llu\nseconds: %.3f\nstate: %u\nkills: %u\nupgrades: %u\nbossDefeated: %s\nenemies: %u\nprojectiles: %u\npickups: %u\nphysicsBodies: %u\nphysicsColliders: %u\nphysicsJoints: %u\nthirdPartyBytes: %llu\nthirdPartyPeakBytes: %llu\ncoreLiveBytes: %llu\ncorePeakBytes: %llu\ncoreAllocations: %llu\nsamples: %zu\nframeP95Ms: %.6f\nframeP99Ms: %.6f\ncpuP95Ms: %.6f\ngpuTimingSource: RHI-latest-completed-frame\n",
        simulate     ? "SimulationOnly"
            : stress ? "VulkanStress"
                     : "VulkanGame",
        static_cast<unsigned long long>(frames),
        final.seconds,
        unsigned(final.state),
        final.kills,
        final.upgrades,
        final.bossDefeated ? "true" : "false",
        final.enemies,
        final.projectiles,
        final.pickups,
        physics.bodies,
        physics.colliders,
        physics.joints,
        static_cast<unsigned long long>(physics.thirdPartyBytes),
        static_cast<unsigned long long>(physics.thirdPartyPeakBytes),
        static_cast<unsigned long long>(memory.liveBytes),
        static_cast<unsigned long long>(memory.peakBytes),
        static_cast<unsigned long long>(memory.allocationCount - memoryBaseline.allocationCount),
        frameTimes.count(),
        p95,
        p99,
        cpu95);
    {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (saving || saveDirty) {
            if (Clock::now() > deadline || !flushSave())
                return 1;
            if (saving && store.take(saving, saved)) {
                saving = {};
                if (!saved.success)
                    return 1;
            }
            std::this_thread::yield();
        }
    }
    if (!capturePath.empty() && !captured)
        return 1;
    if (scenario == "death")
        return final.state == RunState::Defeat ? 0 : 1;
    if (scenario == "evolution"
        && (final.upgrades < 18 || weapons.boltShots <= 1 || !weapons.boltPierces
            || !weapons.boltBounces || weapons.orbitBlades <= config.baseOrbitBlades
            || weapons.pulseRadiusPercent <= 100))
        return 1;
    if (stress && secondsLimit >= config.warmup + config.sample) {
        const bool accepted = frameTimes.count() > 0 && gpuTimes.count() > 0 && p95 <= 16.67
            && p99 <= 33.33 && final.enemies == config.stressEnemies
            && final.projectiles == config.stressProjectiles
            && final.pickups == config.stressPickups;
        std::printf("performanceAcceptance: %s\n", accepted ? "Passed" : "Failed");
        if (!accepted)
            return 1;
    }
    return !normalRestarts && autoplay && final.state != RunState::Victory
            && (!framesLimit && !secondsLimit)
        ? 1
        : 0;
}

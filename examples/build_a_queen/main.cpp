#include "game.h"
#include "hud_renderer.h"
#include "platform/render_system.h"
#include "platform/renderdoc_capture_scope.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "scene_renderer.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string_view>

namespace {
using namespace arp;

bool parseUnsigned(const char* value, std::uint64_t& output)
{
    const std::string_view text(value);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), output);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

int runNullSmoke()
{
    auto device = rhi::createNullDevice();
    if (!device)
        return 1;
    const auto report = rhi::NullHiddenFrameSmokeRunner(*device).run(
        {"BuildAQueenContract", {1280, 720}, rhi::TextureFormat::Bgra8Unorm, 2});
    if (!report.success) {
        std::fprintf(stderr, "%s\n", report.diagnostics.c_str());
        return 1;
    }
    build_a_queen::Game game;
    if (!game.start(1))
        return 1;
    for (std::uint32_t frame = 0; frame < 8; ++frame)
        if (!game.update({}, 0.25f))
            return 1;
    std::printf(
        "type: arp.build-a-queen.smoke\ncontractVersion: 1\nstatus: ContractComplete\nbackend: NullValidatorOnly\nsubmittedFrames: %u\nphase: %s\ndistance: %.2f\n",
        report.submittedFrameCount,
        build_a_queen::phaseName(game.snapshot().phase),
        game.snapshot().distance);
    return report.submittedFrameCount == 2 && game.snapshot().phase == build_a_queen::Phase::Running
        ? 0
        : 1;
}

} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    using Clock = std::chrono::steady_clock;
    bool hidden = false;
    bool autoplay = false;
    bool nullSmoke = false;
    std::uint64_t frameLimit = 0;
    std::uint64_t captureFrame = UINT64_MAX;
    core::pmr::String capturePath;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--hidden")
            hidden = true;
        else if (argument == "--autoplay")
            autoplay = true;
        else if (argument == "--null-smoke")
            nullSmoke = true;
        else if (argument == "--frames" || argument == "--capture-frame") {
            if (++index >= argc)
                return 2;
            std::uint64_t value = 0;
            if (!parseUnsigned(argv[index], value))
                return 2;
            if (argument == "--frames")
                frameLimit = value;
            else
                captureFrame = value;
        } else if (argument == "--capture-path" && index + 1 < argc) {
            capturePath = argv[++index];
        } else {
            std::fprintf(stderr, "ARP-QUEEN-ARGUMENT: %s\n", argv[index]);
            return 2;
        }
    }
    if (nullSmoke)
        return runNullSmoke();
    if (!frameLimit && captureFrame != UINT64_MAX && capturePath.empty())
        return 2;

    const auto executableDir = std::filesystem::absolute(argv[0]).parent_path();
    const auto shaderPath =
        core::pmr::String((executableDir / "shaders/queen_scene.slang").string().c_str());
    const auto hudShaderPath =
        core::pmr::String((executableDir / "shaders/gui_overlay.slang").string().c_str());
    platform::WindowSystem windows;
    if (!windows.initialize().success)
        return 1;
    auto window = windows.createWindow({"Build A Queen", 1280, 720, hidden, true});
    if (!window.valid())
        return 1;
    auto renderSystem = platform::createDefaultRenderSystem(
        windows, window, "Build A Queen", rhi::PresentationMode::VSync);
    if (!renderSystem.valid()) {
        std::fprintf(stderr, "%s\n", renderSystem.diagnostics().c_str());
        return 1;
    }

    build_a_queen::SceneRenderer renderer(*renderSystem.gpuDevice());
    const auto characterDirectory =
        core::pmr::String((executableDir / "assets/characters").string().c_str());
    auto result = renderer.create(
        shaderPath, characterDirectory, renderSystem.colorFormat(), renderSystem.extent());
    if (!result.success) {
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
        return 1;
    }
    build_a_queen::HudRenderer hud(*renderSystem.gpuDevice());
    result = hud.create(hudShaderPath, renderSystem.colorFormat(), renderSystem.extent());
    if (!result.success) {
        std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
        return 1;
    }
    build_a_queen::Game game;
    if (autoplay && !game.start(1))
        return 1;

    platform::WindowEventBatch events;
    platform::RenderDocCaptureScope capture;
    bool closeRequested = false;
    bool captureDone = false;
    bool pointerHeld = false;
    float lastDragX = 0.0f;
    std::uint64_t frames = 0;
    auto previous = Clock::now();
    while (!closeRequested && (!frameLimit || frames < frameLimit)) {
        windows.pumpEvents(events);
        build_a_queen::Input input;
        for (const auto& event : events.windowEvents) {
            if (event.type == platform::WindowEventType::CloseRequested)
                closeRequested = true;
            if (event.type == platform::WindowEventType::Resized && event.width && event.height) {
                const rhi::Extent2D extent{static_cast<std::uint32_t>(window.pixelWidth()),
                    static_cast<std::uint32_t>(window.pixelHeight())};
                const auto resized = renderSystem.resize(extent);
                if (!resized.success) {
                    std::fprintf(stderr, "%s\n", resized.diagnostics.c_str());
                    return 1;
                }
                result = renderer.resize(extent);
                if (!result.success) {
                    std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
                    return 1;
                }
                result = hud.resize(extent);
                if (!result.success) {
                    std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
                    return 1;
                }
            }
        }
        events.windowEvents.clear();
        for (const auto& event : events.inputEvents) {
            if (event.kind == platform::WindowInputEvent::Kind::FocusLost) {
                pointerHeld = false;
                continue;
            }
            if (event.kind == platform::WindowInputEvent::Kind::MouseButtonDown
                && event.mouseButton == platform::WindowMouseButton::Left) {
                pointerHeld = true;
                lastDragX = event.x;
                continue;
            }
            if (event.kind == platform::WindowInputEvent::Kind::MouseButtonUp
                && event.mouseButton == platform::WindowMouseButton::Left) {
                pointerHeld = false;
                continue;
            }
            if (event.kind == platform::WindowInputEvent::Kind::PointerMoved && pointerHeld) {
                const float threshold = float(window.width()) / 6.0f;
                const float deltaX = event.x - lastDragX;
                if (std::abs(deltaX) >= threshold) {
                    input.laneDelta += deltaX < 0.0f ? -1 : 1;
                    lastDragX = event.x;
                }
                continue;
            }
            if (event.kind != platform::WindowInputEvent::Kind::KeyDown)
                continue;
            if (event.key == platform::WindowKey::A || event.key == platform::WindowKey::ArrowLeft)
                input.laneDelta = -1;
            if (event.key == platform::WindowKey::D || event.key == platform::WindowKey::ArrowRight)
                input.laneDelta = 1;
            if (event.key == platform::WindowKey::Enter
                || event.key == platform::WindowKey::Space) {
                input.start = game.snapshot().phase == build_a_queen::Phase::Menu;
                input.advance = game.snapshot().phase == build_a_queen::Phase::Result;
            }
            if (event.key == platform::WindowKey::R)
                input.restart = true;
            if (event.key == platform::WindowKey::Escape)
                closeRequested = true;
        }
        events.inputEvents.clear();

        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration<float>(now - previous).count();
        previous = now;
        const auto delta = autoplay ? 1.0f / 60.0f : elapsed;
        if (autoplay && game.snapshot().phase == build_a_queen::Phase::Menu)
            input.start = true;
        if (!game.update(input, std::clamp(delta, 0.0f, 0.25f)))
            return 1;
        if (!renderer.readyForFrame(frames))
            continue;

        const bool captureNow = !captureDone && frames == captureFrame;
        if (captureNow && !capture.begin(capturePath)) {
            std::fputs("ARP-QUEEN-RENDERDOC-NOT-INJECTED\n", stderr);
            return 1;
        }
        auto* swapchain = renderSystem.swapchain();
        const auto frame = swapchain->acquireNextFrame();
        if (!frame.success) {
            if (frame.recoverable)
                continue;
            std::fprintf(stderr, "%s\n", frame.diagnostics.c_str());
            return 1;
        }
        auto image = swapchain->acquiredImage(frame);
        if (!image.success || !image.texture)
            return 1;
        result = renderer.draw(*image.texture,
            frame,
            game.snapshot(),
            game.levelDesc(),
            game.pickups(),
            game.resolvedPickups(),
            game.pickupCount(),
            hud,
            frames);
        if (!result.success) {
            std::fprintf(stderr, "%s\n", result.diagnostics.c_str());
            return 1;
        }
        const auto presented = swapchain->present(frame);
        if (!presented.success) {
            if (!presented.recoverable) {
                std::fprintf(stderr, "%s\n", presented.diagnostics.c_str());
                return 1;
            }
        }
        if (captureNow) {
            renderSystem.waitGpuIdle();
            if (!capture.end())
                return 1;
            captureDone = true;
        }
        ++frames;
    }
    renderSystem.waitGpuIdle();
    if (captureFrame != UINT64_MAX && !captureDone)
        return 1;
    std::printf(
        "type: arp.build-a-queen.run\nframes: %llu\nphase: %s\nlevel: %u\nvertices: %u\nhudDraws: %u\n",
        static_cast<unsigned long long>(frames),
        build_a_queen::phaseName(game.snapshot().phase),
        game.snapshot().level,
        renderer.lastVertexCount(),
        hud.lastDrawCount());
    return frames > 0 ? 0 : 1;
}

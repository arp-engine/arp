#include "core/diagnostic_error.h"
#include "platform/render_system.h"
#include "platform/render_system_ordered_profile.h"
#include "portable_renderer.h"

#include <algorithm>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>

using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi::semantics;
using namespace arp::build_a_queen;

namespace {
// clang-format off
EM_JS(void, publishState, (int phase, int lane, double distance, int collected, int score, int won, int frames, int vertices, int level, int outfit, int equippedMask, int resolved), {
    Module.queenState={phase,lane,distance,collected,score,won:!!won,frames,vertices,level,outfit,equippedMask,resolved,backend:'WebGL1',prototype:true};
});
EM_JS(void, failPage, (const char* reason), { queenFail(UTF8ToString(reason)); });
EM_JS(void, readyPage, (), { requestAnimationFrame(queenReady); });
EM_JS(void, publishPlatform, (int backend, int ordered, int lifecycle, int completion), {
    Module.queenPlatform={engineRenderSystem:true,backend,orderedGraphics:!!ordered,
        surfaceLifecycle:!!lifecycle,gpuCompletionClaimed:!!completion};
});
// clang-format on

class QueenBrowser final {
public:
    void create();
    void tick();

    void action(int command)
    {
        if (command == 0)
            input_.start = input_.advance = true;
        if (command == 1)
            input_.laneDelta = -1;
        if (command == 2)
            input_.laneDelta = 1;
        if (command == 3)
            input_.restart = true;
    }

    void pointer(double x)
    {
        const int lane = std::clamp(int(x / (1280.0 / 3)), 0, 2);
        input_.laneDelta = lane - game_.snapshot().lane;
        if (game_.snapshot().phase != Phase::Running)
            action(0);
    }

private:
    std::shared_ptr<RenderPipeline> pipeline(const char*, int);
    platform::RenderSystem system_;
    core::UniquePtr<PortableQueenRenderer> renderer_;
    Game game_;
    Input input_;
    double previous_ = 0, seconds_ = 0;
    int frames_ = 0;
};

core::UniquePtr<QueenBrowser> app;

std::shared_ptr<RenderPipeline> QueenBrowser::pipeline(const char* name, int kind)
{
    platform::OrderedPipelineLoadDesc request;
    request.artifactPath = "/artifacts/";
    request.artifactPath += name;
    request.artifactPath += ".webgl-program.yaml";
    request.artifactBudget = {4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32};
    request.maxUniformLeaves = 256;
    request.maxInterfaces = 32;
    request.maxTypeNodes = 1024;
    request.maxInterfaceNameBytes = 1024;
    request.maxDiagnosticBytes = 65536;
    auto& definition = request.layout;
    // PreparedGraphicsCommands/4 shares set/stream slot topology across the
    // catalog. These stages declare set 0 with no resources or GPU payload.
    if (kind == 0 || kind == 1) {
        definition.descriptorLayouts.push_back({"QueenEmptySet", {}});
        definition.descriptorSets.push_back({0, "QueenEmptySet"});
    }
    if (kind == 3) {
        request.useReflectedLayout = false;
        definition = {};
        definition.descriptorLayouts.push_back({"GuiBitmapSet",
            {{0, DescriptorType::SampledTexture, ShaderStage::Fragment},
                {1, DescriptorType::Sampler, ShaderStage::Fragment}}});
        definition.descriptorSets.push_back({0, "GuiBitmapSet"});
        definition.pushConstantRanges.push_back({ShaderStage::Vertex, 0, 64});
    }
    auto& desc = request.pipeline;
    desc.colorFormat = TextureFormat::Rgba8Unorm;
    desc.dynamicViewportScissor = true;
    desc.cullMode = CullMode::None;
    desc.frontFace = FrontFace::CounterClockwise;
    if (kind != 3) {
        desc.depthFormat = desc.depthStencil.format = TextureFormat::Depth24Stencil8;
        desc.depthStencil.depthTestEnabled = kind != 0;
        desc.depthStencil.depthWriteEnabled = kind != 0;
        desc.depthStencil.depthCompare = CompareOp::LessEqual;
    }
    ColorTargetState color;
    color.format = desc.colorFormat;
    if (kind == 3) {
        color.blendEnabled = true;
        color.colorBlend =
            color.alphaBlend = {BlendFactor::One, BlendFactor::OneMinusSrcAlpha, BlendOp::Add};
    }
    desc.colorTargets.push_back(color);
    VertexBufferLayoutDesc stream;
    if (kind == 0 || kind == 3) {
        stream.strideBytes = sizeof(core::Vec2);
        stream.attributes = {{0, 0, VertexFormat::Float32x2}};
    } else {
        stream.strideBytes = sizeof(SceneContent::Vertex);
        stream.attributes = {
            {0, offsetof(SceneContent::Vertex, position), VertexFormat::Float32x3}};
        if (kind == 2) {
            stream.attributes.push_back(
                {1, offsetof(SceneContent::Vertex, normal), VertexFormat::Float32x3});
            stream.attributes.push_back(
                {2, offsetof(SceneContent::Vertex, color), VertexFormat::Float32x4});
        }
    }
    desc.vertexBuffers.push_back(std::move(stream));
    core::pmr::String diagnostic;
    auto result = system_.createOrderedPipeline(request, diagnostic);
    if (!result)
        throw core::DiagnosticError(diagnostic);
    return result;
}

void QueenBrowser::create()
{
    constexpr std::uint64_t MiB = 1024 * 1024;
    platform::RenderSystemOrderedProfile profile;
    profile.contractVersion = 1;
    profile.prototypeOptIn = true;
    profile.sampledDepthOptIn = true;
    auto& b = profile.budgets;
    b.contextMetadataBytes = 256 * 1024;
    b.resourceMetadataBytes = 16 * MiB;
    b.uploadBytes = 64 * MiB;
    b.logicalBufferBytes = b.logicalTextureBytes = 32 * MiB;
    b.resourceObjects = 256;
    b.deviceMetadataBytes = MiB;
    b.identityFieldBytes = 4096;
    b.surfaceMutations = 256;
    b.pipelines = {1, "queen-pipelines", 4};
    b.descriptors = {1, "queen-descriptors", 16};
    auto& request = profile.execution;
    request.contractVersion = 1;
    request.prototypeOptIn = true;
    request.maxSubmissions = 1000000;
    request.maxCpuMetadataBytes = MiB;
    request.requirements = {1, 1, 1280, 1024, 3, 36, 40, 0, 1};
    auto& pd = profile.frames;
    pd.contractVersion = 1;
    pd.prototypeOptIn = true;
    pd.surface = {"#canvas", PortableQueenDesc::kExtent, TextureFormat::Rgba8Unorm, 0};
    pd.commands = {
        4, true, 516, 256 * 1024, 1, {1, true, 540, 128, 256 * 1024, 8 * MiB, 4096}, 4, 4, 5, 64};
    pd.maxFrameCpuMetadataBytes = 8 * MiB;
    pd.frameQuota = core::makeShared<core::CpuMemoryQuota>(pd.maxFrameCpuMetadataBytes);
    pd.commandQuota = core::makeShared<core::CpuMemoryQuota>(pd.commands.issue.maxCpuMetadataBytes);
    platform::RenderSystemDesc creation;
    creation.backendPreference = platform::BackendPreference::WebGL;
    creation.applicationName = "BuildAQueen";
    creation.orderedGraphics = &profile;
    system_ = platform::createRenderSystem(creation);
    if (!system_.valid())
        throw core::DiagnosticError(system_.diagnostics());
    const auto capabilities = system_.orderedGraphicsCapabilities();
    publishPlatform(int(system_.backend()),
        capabilities.orderedGraphics,
        capabilities.surfaceLifecycle,
        capabilities.gpuCompletion);
    PortableQueenDesc desc;
    desc.execution = system_.orderedGraphicsExecution();
    const std::array<const char*, 4> names{
        "queen_portable_sky", "queen_portable_shadow", "queen_portable", "gui_bitmap"};
    for (int i = 0; i < 4; ++i)
        desc.pipelines[i] = pipeline(names[i], i);
    desc.attachmentSampleFlipY = capabilities.attachmentSampleFlipY;
    renderer_ = core::makeUnique<PortableQueenRenderer>(*system_.gpuDevice(), std::move(desc));
    core::requireSuccessfulResult(renderer_->create("/assets/characters"), "QUEEN-CONTENT");
    readyPage();
}

void QueenBrowser::tick()
try {
    core::requireSuccessfulResult(system_.synchronizeSurfaceLifecycle(), "QUEEN-SURFACE");
    const double now = emscripten_get_now();
    if (!system_.capabilities().canRender) {
        previous_ = 0;
        return;
    }
    const float delta = previous_ ? float(std::clamp((now - previous_) / 1000, 0.0, .1)) : 0;
    previous_ = now;
    seconds_ += delta;
    if (!game_.update(input_, delta))
        throw core::DiagnosticError("QUEEN-GAME-UPDATE");
    input_ = {};
    core::requireSuccessfulResult(renderer_->update(game_, float(seconds_)), "QUEEN-FRAME");
    const auto report = system_.renderOrderedFrame(*renderer_);
    if (!report.success)
        throw core::DiagnosticError(report.diagnostics);
    const auto& state = game_.snapshot();
    int outfit = 0, equipped = 0, resolved = 0;
    for (std::size_t i = 0; i < state.outfit.size(); ++i) {
        outfit |= int(state.outfit[i]) << (i * 3);
        equipped |= int(state.equipped[i]) << i;
    }
    for (std::uint32_t i = 0; i < game_.pickupCount(); ++i)
        resolved += game_.resolvedPickups()[i];
    publishState(int(state.phase),
        state.lane,
        state.distance,
        state.collected,
        state.styleScore,
        state.won,
        ++frames_,
        renderer_->worldVertices(),
        state.level,
        outfit,
        equipped,
        resolved);
} catch (const std::exception& error) {
    failPage(error.what());
    emscripten_cancel_main_loop();
}

EM_BOOL keyboard(int, const EmscriptenKeyboardEvent* event, void*)
{
    if (event->repeat)
        return EM_FALSE;
    const core::pmr::String key(event->key);
    if (key == "ArrowLeft" || key == "a" || key == "A")
        app->action(1);
    else if (key == "ArrowRight" || key == "d" || key == "D")
        app->action(2);
    else if (key == "Enter" || key == " ")
        app->action(0);
    else if (key == "r" || key == "R")
        app->action(3);
    else
        return EM_FALSE;
    return EM_TRUE;
}

EM_BOOL mouse(int type, const EmscriptenMouseEvent* event, void*)
{
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN || event->buttons) {
        double width = 0, height = 0;
        emscripten_get_element_css_size("#canvas", &width, &height);
        app->pointer(event->targetX * 1280 / width);
        return EM_TRUE;
    }
    return EM_FALSE;
}

EM_BOOL touch(int, const EmscriptenTouchEvent* event, void*)
{
    if (event->numTouches) {
        double width = 0, height = 0;
        emscripten_get_element_css_size("#canvas", &width, &height);
        app->pointer(event->touches[0].targetX * 1280 / width);
    }
    return EM_TRUE;
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void queen_action(int command)
{
    if (app)
        app->action(command);
}

int main()
try {
    app = core::makeUnique<QueenBrowser>();
    app->create();
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, true, keyboard);
    emscripten_set_mousedown_callback("#canvas", nullptr, true, mouse);
    emscripten_set_mousemove_callback("#canvas", nullptr, true, mouse);
    emscripten_set_touchstart_callback("#canvas", nullptr, true, touch);
    emscripten_set_touchmove_callback("#canvas", nullptr, true, touch);
    emscripten_set_main_loop(
        [] {
            app->tick();
        },
        0,
        false);
    return 0;
} catch (const std::exception& error) {
    failPage(error.what());
    return 1;
}

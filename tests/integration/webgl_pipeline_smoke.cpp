#include "rhi/backends/webgl/webgl_descriptors_internal.h"
#include "rhi/backends/webgl/webgl_device.h"
#include "rhi/backends/webgl/webgl_execution.h"
#include "rhi/backends/webgl/webgl_pipeline_internal.h"
#include "rhi/gpu_device.h"
#include "webgl_basic_forward_smoke.h"
#include "webgl_command_budget_smoke.h"
#include "webgl_conformance_clear_smoke.h"
#include "webgl_conformance_sampled_smoke.h"
#include "webgl_cpu_retirement_smoke.h"
#include "webgl_execution_quality_smoke.h"
#include "webgl_extension_execution_smoke.h"
#include "webgl_gui_smoke.h"
#include "webgl_ordered_graphics_smoke.h"
#include "webgl_scene2d_smoke.h"
#include "webgl_surface_lifecycle_smoke.h"

#include <GLES2/gl2.h>
#include <emscripten.h>
#include <emscripten/html5.h>

using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi::semantics;

namespace {
EM_JS(void, snapshotRuntimeEnvironment, (), {
    // clang-format off
    const gl=GL.currentContext.GLctx;
    const debug=gl.getExtension('WEBGL_debug_renderer_info');
    Module.arpRuntimeEnvironment={userAgent:navigator.userAgent,platform:navigator.platform,
        version:gl.getParameter(gl.VERSION),shaderLanguage:gl.getParameter(gl.SHADING_LANGUAGE_VERSION),
        vendor:gl.getParameter(gl.VENDOR),renderer:gl.getParameter(gl.RENDERER),
        unmaskedVendor:debug?gl.getParameter(debug.UNMASKED_VENDOR_WEBGL):null,
        unmaskedRenderer:debug?gl.getParameter(debug.UNMASKED_RENDERER_WEBGL):null,
        contextAttributes:gl.getContextAttributes(),supportedExtensions:gl.getSupportedExtensions().sort(),
        webGpuIsolation:'test-only navigator.gpu unavailable; MIN/MAX_WEBGL_VERSION=1'};
    // clang-format on
});

EM_JS(void,
    reportPipeline,
    (int success,
        const char* diagnostic,
        int variants,
        int repeatedFrames,
        int browserFrames,
        int recovered,
        int surfaceFrames,
        int surfaceValidated,
        int retirementValidated),
    {
        // clang-format off
    const parameters = new URLSearchParams(location.search);
    const full = !!success && !parameters.has('surfaceOnly') && !parameters.has('retirementOnly');
    fetch('/result',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:parameters.has('retirementOnly') ? 'WebGlCpuRetirementSmoke' : parameters.get('surfaceOnly') === '1' ? 'WebGlSurfaceLifecycleSmoke' : 'WebGlPipelineSmoke',
        contractVersion:1,success:!!success,diagnostic:UTF8ToString(diagnostic),
        surfaceFrames,surfaceLifecycle:!!surfaceValidated,cpuRetirement:!!retirementValidated,
        variants,repeatedFrames,browserFrames,contextRecovered:!!recovered,
        recoveryRequeryFailureValidated:full && parameters.get('recoveryFault') === '1',
        recoveryBudgetValidated:full && parameters.get('recoveryFault') !== '1',
        webGpuUnavailable:navigator.gpu===undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        productionBackendValidated:false,visualAcceptanceClaimed:false,gpuCompletionClaimed:false,
        quality:Module.webGlQuality || null,
        rafStamps:Module.arpRafStamps || [],repeatedRafTimestamps:Module.arpRepeatedRafStamps || 0,
        preparedDraws:full?2:0,immutableConstants:full,queuePreflight:full,originalDevice:!!success,pipelineCatalog:full,multiPass:full,offscreenSampling:full,canvasLease:full})});
        Module.arpWorkloadFinished?.();
        // clang-format on
    });

// Test-only native fault: the actual framebuffer unbind executes, then an
// explicitly invalid native enum creates a real GL error. No production hook.
EM_JS(void, faultOnFinalUnbind, (), {
    // clang-format off
    const context = GL.currentContext.GLctx;
    const original = context.bindFramebuffer.bind(context);
    context.bindFramebuffer = (target, framebuffer) => {
        original(target, framebuffer);
        if (framebuffer === null) {
            context.bindFramebuffer = original;
            context.enable(0xffffffff);
        }
    };
    // clang-format on
});

EM_JS(void, resizeCanvasForOracle, (int width), {
    // clang-format off
    Module.canvas.width=width;
    // clang-format on
});

// Asyncify belongs to this validation executable, never the backend libraries.
EM_ASYNC_JS(double, nextBrowserFrame, (), {
    // clang-format off
    Module.arpAsyncWorkload = true;
    Module.arpStage = 'RAF';
    return await new Promise((resolve, reject) => {
        const stamps = Module.arpRafStamps ||= [];
        const previous = stamps.length ? stamps[stamps.length - 1] : -Infinity;
        let pending;
        const timer = setTimeout(() => {
            cancelAnimationFrame(pending);
            reject(Error('RAF-TIMEOUT'));
        }, 10000);
        const observe = stamp => {
            if (!Number.isFinite(stamp) || stamp < previous) {
                clearTimeout(timer);
                reject(Error('RAF-TIMESTAMP-REGRESSION'));
            } else if (stamp === previous) {
                // Equal timestamps identify no new frame. Keep the same deadline.
                Module.arpRepeatedRafStamps = (Module.arpRepeatedRafStamps || 0) + 1;
                pending = requestAnimationFrame(observe);
            } else {
                clearTimeout(timer);
                stamps.push(stamp);
                resolve(stamp);
            }
        };
        pending = requestAnimationFrame(observe);
    });
    // clang-format on
});

EM_JS(int, syntheticLossNow, (), {
    // clang-format off
    const ext = GL.currentContext.GLctx.getExtension('WEBGL_lose_context');
    if (!ext) return 0;
    Module.arpLossExtension = ext;
    ext.loseContext();
    return 1;
    // clang-format on
});

EM_ASYNC_JS(int, syntheticRestoreAfterLoss, (), {
    // clang-format off
    Module.arpAsyncWorkload = true;
    Module.arpStage = 'Loss';
    const context = GL.currentContext.GLctx;
    const ext = Module.arpLossExtension;
    if (!ext) return 0;
    const wait = type => new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            context.canvas.removeEventListener(type, listener, false);
            reject(Error('RECOVERY-EVENT-TIMEOUT:' + type));
        }, 10000);
        const listener = event => {clearTimeout(timer); context.canvas.removeEventListener(type, listener, false); resolve(event);};
        context.canvas.addEventListener(type, listener, false);
    });
    const lost = wait('webglcontextlost');
    await lost;
    // Exit the browser's loss-event dispatch before requesting restoration.
    // Its cancellation result is committed after the dispatch returns.
    await new Promise(resolve => setTimeout(resolve, 0));
    Module.arpStage = 'Restore';
    const restored = wait('webglcontextrestored');
    ext.restoreContext();
    await restored;
    Module.arpLossExtension = null;
    Module.arpStage = 'NativeRestored';
    return 1;
    // clang-format on
});

EM_JS(int, recoveryFaultMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('recoveryFault') === '1' ? 1 : 0;
    // clang-format on
});

EM_JS(int, surfaceOnlyMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('surfaceOnly') === '1' ? 1 : 0;
    // clang-format on
});

EM_JS(int, retirementOnlyMode, (), {
    // clang-format off
    return Number(new URLSearchParams(location.search).get('retirementOnly') || 0);
    // clang-format on
});

// Test-only: the native query still runs, then a real invalid GL enum makes
// requery fail. This is never a production recovery/fallback hook.
EM_JS(void, faultOnRecoveryQuery, (), {
    // clang-format off
    const context = GL.currentContext.GLctx;
    const original = context.getParameter.bind(context);
    context.getParameter = parameter => {
        context.getParameter = original;
        const value = original(parameter);
        context.enable(0xffffffff);
        return value;
    };
    // clang-format on
});

EM_ASYNC_JS(void, restoredCaptureGate, (), {
    // clang-format off
    if (new URLSearchParams(location.search).get('recoveryGate') === '1') {
        await fetch('/ready', {method:'POST'});
        while (!(await fetch('/armed', {cache:'no-store'})).ok)
            await new Promise(resolve => requestAnimationFrame(resolve));
    }
    // clang-format on
});

bool integer(GLenum key, GLint expected)
{
    GLint observed = 0;
    glGetIntegerv(key, &observed);
    if (observed != expected)
        std::fprintf(
            stderr, "PIPELINE-STATE key=%u actual=%d expected=%d\n", key, observed, expected);
    return observed == expected;
}

class PipelineSmoke final {
public:
    core::pmr::String failureStorage{&core::defaultCpuMemoryResource()};
    const char* failure = "";
    int orderedFrames = 0;
    int conformancePixels = 0;
    bool conformanceSealed = false, conformanceAcceptedOnFailure = false,
         conformanceRetired = false, conformanceLost = false;
    core::pmr::String conformanceHash;
    int variants = 0;
    int repeatedFrames = 0;
    int browserFrames = 0;
    bool recovered = false;
    int surfaceFrames = 0;
    bool surfaceValidated = false;
    bool retirementValidated = false;

    bool check(bool value, const char* diagnostic)
    {
        if (!value) {
            failureStorage = diagnostic ? diagnostic : "PIPELINE-FIXTURE-MISSING-DIAGNOSTIC";
            failure = failureStorage.c_str();
            std::fprintf(stderr, "Pipeline smoke failed: %s\n", failure);
        }
        return value;
    }

    bool run();
    bool recovery(webgl::WebGlDevice&,
        const std::shared_ptr<RenderPipeline>&,
        const WebGlResourceOwnershipRequest&,
        const WebGlDeviceCompositionRequest&,
        const std::shared_ptr<const shader::WebGlShaderProgramArtifact>&);
    bool multipass(webgl::WebGlDevice&, const std::shared_ptr<RenderPipeline>&);
    bool execute(
        webgl::WebGlDevice&, webgl::WebGlResources&, const std::shared_ptr<RenderPipeline>&);
};

#define REQUIRE(value, diagnostic)                                                                 \
    do {                                                                                           \
        if (!check(bool(value), diagnostic))                                                       \
            return false;                                                                          \
    } while (false)

bool PipelineSmoke::run()
{
    shader::WebGlShaderLoadBudget load{
        {4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32}, 256, 32, 1024};
    auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(load.original.maxCpuMetadataBytes);
    core::pmr::String message{&core::defaultCpuMemoryResource()};
    auto artifact = shader::WebGlShaderProgramArtifact::load(
        "/artifacts/webgl1_graphics.webgl-program.yaml", load, artifactQuota, message);
    REQUIRE(artifact, "PIPELINE-ORIGINAL-LOAD");
    WebGlContextOwnershipRequest context;
    context.contractVersion = context.context.contractVersion = 1;
    context.canvasSelector = "#canvas";
    context.colorSpace = WebGlCanvasColorSpace::SRgb;
    context.maxCpuMetadataBytes = 256 * 1024;
    context.maxIdentityFieldBytes = 4096;
    if (webgl::test::basicForwardMode()) {
        context.extensionOptIn.contractVersion = 1;
        context.extensionOptIn.extensions.depthTextures = true;
    }
    auto contextQuota = core::makeShared<core::CpuMemoryQuota>(context.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    auto runtime = webgl::WebGlRuntime::create(context, contextQuota, diagnostic);
    REQUIRE(runtime, diagnostic ? diagnostic : "PIPELINE-CONTEXT");
    snapshotRuntimeEnvironment();
    WebGlResourceOwnershipRequest ownership;
    ownership.contractVersion = ownership.budget.contractVersion = 1;
    ownership.budget.capacity.cpuMetadataBytes = 4 * 1024 * 1024;
    ownership.budget.capacity.cpuUploadBytes = 256 * 1024;
    ownership.budget.capacity.objectCount = 64;
    ownership.budget.capacity.logicalBufferBytes = 4096;
    ownership.budget.capacity.logicalTextureBytes = 64 * 1024;
    if (webgl::test::basicForwardMode()) {
        ownership.extensions = {1, true, context.extensionOptIn.extensions};
    }
    if (webgl::test::basicForwardMode() || webgl::test::conformanceSampledMode()) {
        ownership.budget.capacity.logicalTextureBytes = 128 * 1024;
        ownership.budget.capacity.logicalBufferBytes = 8192;
        ownership.budget.capacity.objectCount = 128;
    }
    if (webgl::test::conformanceSampledMode() && webgl::test::conformanceSampledVersion() == 2) {
        // Four RGBA8 textures for each 64x64 / 48x32 / 64x64 prepared frame,
        // plus the active canvas lease: 155648 + 16384 logical bytes.
        ownership.budget.capacity.logicalTextureBytes = 256 * 1024;
        // InitializedByteBuffer retains both bytes and byte initialization coverage:
        // texture preparation alone requires 2 * 155648 CPU upload bytes.
        // The explicit negative keeps the previous 256 KiB policy and must fail.
        if (!webgl::test::conformanceSampledBudgetFault())
            ownership.budget.capacity.cpuUploadBytes = 512 * 1024;
    }
    auto metadata =
        core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuMetadataBytes);
    auto uploads = core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes);
    auto resources =
        webgl::WebGlResources::create(std::move(runtime), ownership, metadata, uploads, diagnostic);
    REQUIRE(resources, diagnostic ? diagnostic : "PIPELINE-RESOURCE-DOMAIN");
    ShaderPackageDesc package;
    auto reflected = populateSelectedGraphicsShaderInterface(
        *artifact->vertex().original, artifact->fragment().original.get(), package);
    if (!reflected.success)
        std::fprintf(stderr, "%s\n", reflected.diagnostics.c_str());
    REQUIRE(reflected.success, "PIPELINE-ORIGINAL-HYDRATION");
    auto definition = buildPipelineLayoutDescFromShaderReflection(package.reflection);
    // The shared helper preserves broad existing visibility. This request
    // explicitly selects only the graphics stages reflected by the originals.
    if (!definition.pushConstantRanges.empty())
        definition.pushConstantRanges[0].stages =
            *parseShaderReflectionPushConstantStages(package.reflection.pushConstantStages);
    auto layout = core::shareUnique(resources->createPipelineLayout(definition));
    REQUIRE(layout, resources->diagnostic());
    RenderPipelineDesc state;
    state.layout = layout.get();
    state.vertexShader = package.entrypoints[0];
    state.fragmentShader = package.entrypoints[1];
    state.colorFormat = TextureFormat::Rgba8Unorm;
    state.colorTargets.push_back({TextureFormat::Rgba8Unorm});
    state.depthFormat = state.depthStencil.format = TextureFormat::Depth24Stencil8;
    state.dynamicViewportScissor = true;
    VertexBufferLayoutDesc stream;
    stream.slot = 3;
    for (const auto& input : artifact->vertex().inputs) {
        VertexFormat format;
        switch (input.components) {
        case 1:
            format = VertexFormat::Float32x1;
            break;
        case 2:
            format = VertexFormat::Float32x2;
            break;
        case 3:
            format = VertexFormat::Float32x3;
            break;
        case 4:
            format = VertexFormat::Float32x4;
            break;
        default:
            return check(false, "PIPELINE-FIXTURE-ATTRIBUTE-SHAPE");
        }
        stream.attributes.push_back({input.location, stream.strideBytes, format});
        stream.strideBytes += VertexInputConsumptionContract::formatBytes(format);
    }
    state.vertexBuffers.push_back(std::move(stream));
    WebGlPipelineCreationRequest request{1, {1, 1024, 4096}, {}};
    const auto count = resources->counters().objectCount;
    state.vertexBuffers[0].attributes[0].shaderLocation += 100;
    REQUIRE(!resources->createRenderPipeline(state, layout, artifact, request)
            && resources->counters().objectCount == count,
        "PIPELINE-WRONG-VERTEX-INTERFACE-ROLLBACK");
    state.vertexBuffers[0].attributes[0].shaderLocation -= 100;
    state.fragmentShader.entryPoint = "foreign";
    REQUIRE(!resources->createRenderPipeline(state, layout, artifact, request)
            && resources->counters().objectCount == count,
        "PIPELINE-WRONG-ENTRY-ROLLBACK");
    state.fragmentShader = package.entrypoints[1];
    auto basic =
        core::shareUnique(resources->createRenderPipeline(state, layout, artifact, request));
    REQUIRE(basic, resources->diagnostic());
    auto* original = dynamic_cast<webgl::WebGlRenderPipeline*>(basic.get());
    REQUIRE(original && !webgl::WebGlRenderPipeline::current(*original->impl)
            && basic->validationReport().success && basic->shaderReflection().pushConstantByteCount,
        "PIPELINE-ORIGINAL-NATIVE-PAYLOAD");
    REQUIRE(!webgl::WebGlRenderPipeline::issueState(*original->impl), "PIPELINE-BASIC-ISSUE");
    ++variants;
    REQUIRE(!glIsEnabled(GL_CULL_FACE) && !glIsEnabled(GL_DEPTH_TEST)
            && !glIsEnabled(GL_STENCIL_TEST) && !glIsEnabled(GL_BLEND) && !glIsEnabled(GL_DITHER)
            && !glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) && !glIsEnabled(GL_SAMPLE_COVERAGE)
            && glIsEnabled(GL_SCISSOR_TEST) && integer(GL_DEPTH_FUNC, GL_LEQUAL)
            && integer(GL_STENCIL_REF, 0) && integer(GL_BLEND_SRC_RGB, GL_ONE)
            && integer(GL_BLEND_DST_RGB, GL_ZERO),
        "PIPELINE-EXPLICIT-BASIC-STATE");
    state.cullMode = CullMode::Front;
    state.frontFace = FrontFace::Clockwise;
    state.depthStencil.depthTestEnabled = state.depthStencil.depthWriteEnabled = true;
    state.depthStencil.depthCompare = CompareOp::Greater;
    state.depthStencil.stencilEnabled = true;
    state.depthStencil.front = {StencilOp::Replace,
        StencilOp::IncrementClamp,
        StencilOp::DecrementClamp,
        CompareOp::NotEqual};
    state.depthStencil.back = {
        StencilOp::Invert, StencilOp::Zero, StencilOp::Keep, CompareOp::Equal};
    state.depthStencil.stencilReadMask = 0x7e;
    state.depthStencil.stencilWriteMask = 0x3c;
    state.depthStencil.depthBiasEnabled = true;
    state.depthStencil.depthBiasConstantFactor = 2;
    state.depthStencil.depthBiasSlopeFactor = 3;
    state.colorTargets[0].blendEnabled = true;
    state.colorTargets[0].colorBlend = {
        BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, BlendOp::ReverseSubtract};
    state.colorTargets[0].alphaBlend = {BlendFactor::One, BlendFactor::DstAlpha, BlendOp::Subtract};
    state.colorTargets[0].writeMask = ColorWriteMask::Red | ColorWriteMask::Alpha;
    // Stencil reference queries clamp against the CURRENT attachment's bit
    // count. The depth/stencil-disabled canvas cannot prove an eight-bit ref.
    // Bind real original-domain packed storage for the state oracle.
    auto stateColor = resources->createTexture({"PipelineStateColor",
        {1, 1, 1},
        TextureFormat::Rgba8Unorm,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::ColorAttachment});
    auto stateDepth = resources->createTexture({"PipelineStateDepth",
        {1, 1, 1},
        TextureFormat::Depth24Stencil8,
        TextureUsage::RenderAttachment,
        MemoryUsage::GpuOnly,
        ResourceState::DepthAttachment});
    REQUIRE(stateColor && stateDepth, "PIPELINE-STATE-ORIGINAL-ATTACHMENTS");
    auto stateFramebuffer = resources->createFramebuffer(*stateColor, stateDepth.get());
    REQUIRE(stateFramebuffer, "PIPELINE-STATE-ORIGINAL-FRAMEBUFFER");
    auto* statePayload = dynamic_cast<webgl::WebGlTexture*>(stateColor.get());
    glBindFramebuffer(
        GL_FRAMEBUFFER, statePayload->payload->domain->find(stateFramebuffer->id())->native);
    REQUIRE(integer(GL_STENCIL_BITS, 8), "PIPELINE-STATE-EIGHT-STENCIL-BITS");
    request.stencilReference = 23;
    auto alternate = resources->createRenderPipeline(state, layout, artifact, request);
    REQUIRE(alternate, resources->diagnostic());
    auto* second = dynamic_cast<webgl::WebGlRenderPipeline*>(alternate.get());
    REQUIRE(!webgl::WebGlRenderPipeline::issueState(*second->impl), "PIPELINE-ALTERNATE-ISSUE");
    ++variants;
    GLboolean mask[4]{};
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    REQUIRE(glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_STENCIL_TEST)
            && glIsEnabled(GL_BLEND) && glIsEnabled(GL_POLYGON_OFFSET_FILL)
            && integer(GL_CULL_FACE_MODE, GL_FRONT) && integer(GL_FRONT_FACE, GL_CCW)
            && integer(GL_DEPTH_FUNC, GL_GREATER) && integer(GL_DEPTH_WRITEMASK, 1)
            && integer(GL_STENCIL_REF, 23) && integer(GL_STENCIL_BACK_REF, 23)
            && integer(GL_STENCIL_FUNC, GL_NOTEQUAL) && integer(GL_STENCIL_BACK_FUNC, GL_EQUAL)
            && integer(GL_STENCIL_FAIL, GL_REPLACE) && integer(GL_STENCIL_BACK_FAIL, GL_INVERT)
            && integer(GL_STENCIL_PASS_DEPTH_FAIL, GL_INCR)
            && integer(GL_STENCIL_BACK_PASS_DEPTH_FAIL, GL_ZERO)
            && integer(GL_STENCIL_PASS_DEPTH_PASS, GL_DECR)
            && integer(GL_STENCIL_BACK_PASS_DEPTH_PASS, GL_KEEP)
            && integer(GL_STENCIL_VALUE_MASK, 0x7e) && integer(GL_STENCIL_BACK_VALUE_MASK, 0x7e)
            && integer(GL_STENCIL_WRITEMASK, 0x3c) && integer(GL_STENCIL_BACK_WRITEMASK, 0x3c)
            && integer(GL_BLEND_EQUATION_RGB, GL_FUNC_REVERSE_SUBTRACT)
            && integer(GL_BLEND_EQUATION_ALPHA, GL_FUNC_SUBTRACT)
            && integer(GL_BLEND_SRC_RGB, GL_SRC_ALPHA)
            && integer(GL_BLEND_DST_RGB, GL_ONE_MINUS_SRC_ALPHA)
            && integer(GL_BLEND_SRC_ALPHA, GL_ONE) && integer(GL_BLEND_DST_ALPHA, GL_DST_ALPHA)
            && mask[0] && !mask[1] && !mask[2] && mask[3] && glGetError() == GL_NO_ERROR,
        "PIPELINE-COMPLETE-STATE-LOWERING");
    // Switching back must reset all observable state; no inheritance from the
    // preceding pipeline and no native defaults are execution authority.
    REQUIRE(!webgl::WebGlRenderPipeline::issueState(*original->impl), "PIPELINE-RESET-ISSUE");
    ++variants;
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    REQUIRE(!glIsEnabled(GL_CULL_FACE) && !glIsEnabled(GL_DEPTH_TEST)
            && !glIsEnabled(GL_STENCIL_TEST) && !glIsEnabled(GL_BLEND)
            && !glIsEnabled(GL_POLYGON_OFFSET_FILL) && mask[0] && mask[1] && mask[2] && mask[3]
            && glGetError() == GL_NO_ERROR,
        "PIPELINE-SWITCH-RESETS-FULL-STATE");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    stateFramebuffer.reset();
    stateDepth.reset();
    stateColor.reset();
    // Original descriptor publication, exact resource ownership, transactional
    // writes and arena epochs share the same domain as the native pipelines.
    auto arena = resources->createDescriptorArena(
        {core::pmr::String(&core::defaultCpuMemoryResource()), 1, 3});
    REQUIRE(arena, resources->diagnostic());
    DescriptorSetDesc setDesc{core::pmr::String(&core::defaultCpuMemoryResource()),
        {core::pmr::String("smoke", &core::defaultCpuMemoryResource()),
            core::pmr::Vector<DescriptorBindingDesc>(&core::defaultCpuMemoryResource())}};
    setDesc.layout.bindings.push_back({0, DescriptorType::UniformBuffer, ShaderStage::Vertex});
    setDesc.layout.bindings.push_back({1, DescriptorType::SampledTexture, ShaderStage::Fragment});
    setDesc.layout.bindings.push_back({2, DescriptorType::Sampler, ShaderStage::Fragment});
    auto descriptors = arena->allocateDescriptorSet(setDesc);
    REQUIRE(
        descriptors && !arena->allocateDescriptorSet(setDesc) && !descriptors->hasWrittenBinding(0),
        "DESCRIPTOR-CAPACITY-AND-UNINITIALIZED");
    BufferDesc bufferDesc;
    bufferDesc.sizeBytes = 64;
    bufferDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    auto buffer = resources->createBuffer(bufferDesc);
    REQUIRE(buffer, resources->diagnostic());
    TextureDesc textureDesc;
    textureDesc.extent = {1, 1, 1};
    textureDesc.format = TextureFormat::Rgba8Unorm;
    textureDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    auto texture = resources->createTexture(textureDesc);
    REQUIRE(texture, resources->diagnostic());
    TextureViewDesc viewDesc;
    viewDesc.format = textureDesc.format;
    viewDesc.usage = TextureUsage::Sampled;
    viewDesc.aspect = TextureAspect::Color;
    auto view = resources->createTextureView(*texture, viewDesc);
    REQUIRE(view, resources->diagnostic());
    auto sampler = resources->createSampler({});
    REQUIRE(view && sampler, resources->diagnostic());
    DescriptorBindingWrite write;
    write.type = DescriptorType::UniformBuffer;
    write.resourceKind = DescriptorResourceKind::Buffer;
    write.resource = buffer->id();
    REQUIRE(descriptors->write(write).success && descriptors->validateBinding(0).success,
        "DESCRIPTOR-ORIGINAL-UNIFORM");
    const auto revision = descriptors->contentRevision();
    write.resource = texture->id();
    REQUIRE(!descriptors->write(write).success && descriptors->contentRevision() == revision
            && descriptors->validateBinding(0).success,
        "DESCRIPTOR-REJECT-PRESERVES-ORIGINAL-CELL");
    write.binding = 1;
    write.type = DescriptorType::SampledTexture;
    write.resourceKind = DescriptorResourceKind::TextureView;
    write.resource = view->id();
    REQUIRE(descriptors->write(write).success, "DESCRIPTOR-ORIGINAL-VIEW");
    write.binding = 2;
    write.type = DescriptorType::Sampler;
    write.resourceKind = DescriptorResourceKind::Sampler;
    write.resource = sampler->id();
    REQUIRE(descriptors->write(write).success, "DESCRIPTOR-ORIGINAL-SAMPLER");
    buffer.reset();
    texture.reset();
    view.reset();
    sampler.reset();
    REQUIRE(descriptors->stats().writtenBindings == 3 && descriptors->validateBinding(0).success
            && descriptors->validateBinding(1).success && descriptors->validateBinding(2).success
            && resources->counters().logicalBufferBytes == 64
            && resources->counters().logicalTextureBytes == 4,
        "DESCRIPTOR-RETAINS-ALL-ORIGINAL-OWNERS");
    auto epoch = descriptors->allocationEpochTicket();
    arena->reset();
    REQUIRE(!epoch.isCurrent() && !descriptors->isCurrentAllocation()
            && !descriptors->write(write).success && !arena->stats().allocatedSets,
        "DESCRIPTOR-RESET-INVALIDATES-OLD-SET");
    auto newSet = arena->allocateDescriptorSet(setDesc);
    REQUIRE(newSet && newSet->isCurrentAllocation(), "DESCRIPTOR-NEW-EPOCH");
    arena.reset();
    REQUIRE(!newSet->isCurrentAllocation(), "DESCRIPTOR-ARENA-DESTRUCTION-INVALIDATES");
    newSet.reset();
    descriptors.reset();
    epoch = {};
    REQUIRE(!resources->counters().logicalBufferBytes && !resources->counters().logicalTextureBytes,
        "DESCRIPTOR-LAST-OWNER-REFUNDS-EXACT-CHARGES");
    auto* originalResources = resources.get(); // Test-only original custody observer.
    WebGlDeviceCompositionRequest composition{
        1, true, 256 * 1024, {1, "device-smoke-pipelines", 8}, {1, "device-smoke-descriptors", 32}};
    auto deviceQuota = core::makeShared<core::CpuMemoryQuota>(composition.maxCpuMetadataBytes);
    auto device =
        webgl::WebGlDevice::create(std::move(resources), composition, deviceQuota, diagnostic);
    REQUIRE(device, diagnostic);
    GpuDevice& frontend = *device;
    REQUIRE(frontend.backend() == Backend::WebGL && !frontend.capabilities().core.graphics
            && !frontend.capabilities().features.nativeSync && !frontend.createFence()
            && !frontend.createSemaphore() && !frontend.createTimelineSemaphore()
            && !frontend.createComputePipeline({}) && !frontend.createCommandEncoder({}),
        "DEVICE-UNAVAILABLE-FACTORIES-FAIL-CLOSED");
    REQUIRE(!device->selectPipelineInputs(layout, artifact, original->impl->request),
        device->diagnostic());
    auto deviceState = RenderPipelineStateCopy::copy(original->impl->definition);
    deviceState.shaderPackage = nullptr;
    const auto beforeValidation = originalResources->counters().objectCount;
    REQUIRE(frontend.validateRenderPipeline(deviceState).success
            && originalResources->counters().objectCount == beforeValidation,
        "DEVICE-VALIDATION-NO-NATIVE-PUBLICATION");
    auto devicePipeline = core::shareUnique(frontend.createRenderPipeline(deviceState));
    REQUIRE(devicePipeline && frontend.pipelineCache().budgetStats().configured
            && frontend.descriptorCache().budgetStats().configured,
        device->diagnostic());
    if (webgl::test::conformanceSampledMode()) {
        webgl::test::ConformanceSampledSmoke sampled;
        const auto success = sampled.run(std::move(device),
            devicePipeline,
            package,
            artifact->vertex().uniformVectors,
            artifact->fragment().uniformVectors,
            &nextBrowserFrame);
        orderedFrames = sampled.frames;
        conformancePixels = sampled.verifiedPixels;
        conformanceSealed = sampled.sealedOutputValidated;
        conformanceAcceptedOnFailure = sampled.rejectedAfterSubmit;
        conformanceRetired = sampled.retiredAuditRejected;
        conformanceLost = sampled.contextLossAuditRejected;
        conformanceHash = sampled.hash;
        return check(success, sampled.diagnostic.c_str());
    }
    if (webgl::test::conformanceClearMode()) {
        webgl::test::ConformanceClearSmoke clear;
        const auto success = clear.run(std::move(device), devicePipeline, &nextBrowserFrame);
        orderedFrames = clear.frames;
        conformancePixels = clear.verifiedPixels;
        conformanceSealed = clear.sealedOutputValidated;
        conformanceAcceptedOnFailure = clear.rejectedAfterSubmit;
        conformanceRetired = clear.retiredAuditRejected;
        conformanceHash = clear.hash;
        return check(success, clear.diagnostic.c_str());
    }
    if (webgl::test::basicForwardMode()) {
        webgl::test::BasicForwardSmoke basic;
        const auto success = basic.run(std::move(device), &nextBrowserFrame);
        orderedFrames = basic.frames;
        return check(success, basic.diagnostic.c_str());
    }
    if (webgl::test::guiMode()) {
        webgl::test::GuiSmoke gui;
        const auto success = gui.run(std::move(device), &nextBrowserFrame);
        orderedFrames = gui.frames;
        return check(success, gui.diagnostic.c_str());
    }
    if (webgl::test::scene2dMode()) {
        webgl::test::Scene2DSmoke scene2d;
        const auto success = scene2d.run(std::move(device), &nextBrowserFrame);
        orderedFrames = scene2d.frames;
        return check(success, scene2d.diagnostic.c_str());
    }
    if (webgl::test::orderedGraphicsMode()) {
        webgl::test::OrderedGraphicsSmoke ordered;
        const auto success = ordered.run(std::move(device),
            devicePipeline,
            &nextBrowserFrame,
            &syntheticLossNow,
            &faultOnFinalUnbind);
        orderedFrames = ordered.frames;
        return check(success, ordered.diagnostic.c_str());
    }
    if (retirementOnlyMode()) {
        arp::rhi::tests::WebGlCpuRetirementSmoke retirement;
        REQUIRE(retirement.run(*device,
                    devicePipeline,
                    retirementOnlyMode() == 2,
                    retirementOnlyMode() == 3 ? &syntheticLossNow : nullptr),
            retirement.diagnostic);
    } else {
        arp::rhi::tests::WebGlSurfaceLifecycleSmoke surface;
        REQUIRE(surface.run(*device, devicePipeline, surfaceOnlyMode(), &nextBrowserFrame),
            surface.diagnostic);
        surfaceFrames = surface.frames;
        surfaceValidated = true;
    }
    if (!surfaceOnlyMode() && !retirementOnlyMode()) {
        REQUIRE(!device->enableRecovery({1, true, recoveryFaultMode() ? 2u : 1u}),
            device->diagnostic());
        REQUIRE(multipass(*device, devicePipeline), failure);
        REQUIRE(recovery(*device, devicePipeline, ownership, composition, artifact), failure);
    }
    const auto retired = device->retireCpu({1, true});
    REQUIRE(retired.success && retired.admissionClosed && !retired.gpuCompletionKnown
            && arp::rhi::tests::WebGlCpuRetirementSmoke::rejectedDrain(*device),
        "DEVICE-CPU-RETIREMENT-NO-GPU-DRAIN");
    retirementValidated = true;
    devicePipeline.reset();
    layout.reset();
    artifact.reset();
    REQUIRE(
        original->layout() && artifactQuota->liveBytes(), "PIPELINE-RETAINS-LAYOUT-PUBLICATION");
    device.reset();
    REQUIRE(!deviceQuota->liveBytes(), "DEVICE-EXACT-RETIREMENT");
    REQUIRE(
        webgl::WebGlRenderPipeline::current(*original->impl), "PIPELINE-RETIRED-DOMAIN-REJECTED");
    REQUIRE(
        webgl::WebGlRenderPipeline::issueState(*original->impl), "PIPELINE-RETIRED-ISSUE-REJECTED");
    basic.reset();
    alternate.reset();
    REQUIRE(!metadata->liveBytes() && !uploads->liveBytes() && !contextQuota->liveBytes()
            && !artifactQuota->liveBytes(),
        "PIPELINE-EXACT-RETIREMENT");
    return true;
}

bool PipelineSmoke::recovery(webgl::WebGlDevice& old,
    const std::shared_ptr<RenderPipeline>& pipeline,
    const WebGlResourceOwnershipRequest& ownership,
    const WebGlDeviceCompositionRequest& composition,
    const std::shared_ptr<const shader::WebGlShaderProgramArtifact>& artifact)
{
    GpuDevice& gpu = old;
    BufferDesc bufferDesc;
    bufferDesc.sizeBytes = 16;
    bufferDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    auto buffer = gpu.createBuffer(bufferDesc);
    auto arena = gpu.createDescriptorArena({{}, 1, 5});
    DescriptorSetDesc setDesc;
    setDesc.layout = pipeline->layout()->desc().descriptorLayouts[0];
    auto descriptors =
        arena ? arena->allocateDescriptorSet(setDesc) : core::UniquePtr<DescriptorSet>{};
    SwapchainDesc canvasDesc;
    canvasDesc.surfaceName = "#canvas";
    canvasDesc.extent = {64, 64};
    canvasDesc.format = TextureFormat::Rgba8Unorm;
    canvasDesc.imageCount = 0;
    auto canvas = gpu.createSwapchain(canvasDesc);
    REQUIRE(buffer && descriptors && canvas, old.diagnostic());
    auto frame = canvas->acquireNextFrame();
    auto image = canvas->acquiredImage(frame);
    auto target = old.canvasFramebuffer(*canvas, frame);
    auto state = RenderPipelineStateCopy::copy(
        dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get())->impl->definition);
    state.shaderPackage = nullptr;
    state.depthFormat = state.depthStencil.format = TextureFormat::Unknown;
    auto canvasPipeline = core::shareUnique(gpu.createRenderPipeline(state));
    WebGlPreparedCommandsRequest command;
    command.contractVersion = 1;
    command.prototypeOptIn = true;
    command.maxDraws = 1;
    command.maxUniformBytes = 160;
    command.maxAnalysisIndices = 3;
    command.issue = {1, true, 3, 8, 12, 256 * 1024, 16};
    auto commandQuota = core::makeShared<core::CpuMemoryQuota>(command.issue.maxCpuMetadataBytes);
    REQUIRE(target && canvasPipeline
            && !old.prepareCommand(canvasPipeline, target, command, commandQuota),
        old.diagnostic());
    auto encoder = gpu.createCommandEncoder({});
    RenderPassDesc passDesc;
    passDesc.colorAttachments.push_back({{},
        image.ref.image,
        TextureFormat::Rgba8Unorm,
        LoadOp::Clear,
        StoreOp::Store,
        false,
        {},
        {1, 0, 1, 1}});
    auto pass = encoder ? encoder->beginRenderPass(passDesc) : core::UniquePtr<RenderPassEncoder>{};
    REQUIRE(pass && pass->end().success, "RECOVERY-ORIGINAL-COMMAND-RECORDED");
    auto commands = encoder->finish();
    REQUIRE(commands && old.contextPhase() == WebGlContextPhase::Ready, "RECOVERY-ORIGINAL-READY");
    const auto* oldProfile = &old.profile();
    const char* readyDiagnostic = nullptr;
    REQUIRE(old.enableRecovery({1, true, 1})
            && !old.restoreResources(ownership, {}, {}, readyDiagnostic),
        "RECOVERY-READY-AND-DUPLICATE-CONFIGURATION-REJECTED");
    const auto beforeLost = old.executionTelemetry().execution;
    REQUIRE(syntheticLossNow() && old.contextPhase() == WebGlContextPhase::Lost
            && !descriptors->isCurrentAllocation() && !canvas->isFrameAcquired(frame)
            && !gpu.queue().submit(*commands).hasAcceptedWork(),
        "RECOVERY-NATIVE-LOSS-IMMEDIATELY-INVALIDATES-OLD-GENERATION");
    const auto lostReport = old.executionTelemetry();
    REQUIRE(lostReport.complete && lostReport.execution.lost == beforeLost.lost + 1
            && lostReport.execution.accepted == beforeLost.accepted
            && !lostReport.gpuCompletionKnown,
        "QUALITY-LOST-COUNTER-ORIGINAL-DOMAIN");
    webgl::test::observeOutcome("native-loss", beforeLost, lostReport.execution);
    REQUIRE(syntheticRestoreAfterLoss(), "RECOVERY-NATIVE-TEST-EXTENSION");
    REQUIRE(old.contextPhase() == WebGlContextPhase::RestorePending
            && !descriptors->isCurrentAllocation() && !canvas->isFrameAcquired(frame)
            && !gpu.queue().submit(*commands).hasAcceptedWork()
            && !gpu.queue().submit(*commands).success,
        "RECOVERY-NATIVE-RESTORED-DOES-NOT-REVIVE-OLD-DOMAIN");
    const char* diagnostic = nullptr;
    auto rejected = old.restoreResources(ownership,
        core::makeShared<core::CpuMemoryQuota>(1),
        core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes),
        diagnostic);
    REQUIRE(!rejected && diagnostic && old.contextPhase() == WebGlContextPhase::RestorePending,
        "RECOVERY-QUOTA-REJECTED-BEFORE-NATIVE-ATTEMPT");
    auto metadata =
        core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuMetadataBytes);
    auto uploads = core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes);
    auto resources = old.restoreResources(ownership, metadata, uploads, diagnostic);
    REQUIRE(resources && old.contextPhase() == WebGlContextPhase::Ready
            && resources->profile().valid() && !resources->currentDiagnostic()
            && &old.profile() == oldProfile && &resources->profile() != oldProfile,
        diagnostic);
    REQUIRE(!old.restoreResources(ownership, metadata, uploads, diagnostic) && diagnostic
            && !gpu.createBuffer(bufferDesc) && !gpu.queue().submit(*commands).success
            && !descriptors->isCurrentAllocation() && !canvas->isFrameAcquired(frame),
        "RECOVERY-OLD-GENERATION-STAYS-STALE-AFTER-READY");
    DescriptorBindingWrite write;
    write.resource = buffer->id();
    write.type = DescriptorType::UniformBuffer;
    write.resourceKind = DescriptorResourceKind::Buffer;
    REQUIRE(!descriptors->write(write).success
            && webgl::WebGlRenderPipeline::current(
                *dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get())->impl),
        "RECOVERY-OLD-DESCRIPTOR-AND-PIPELINE-REJECTED");
    auto* originalResources = resources.get();
    auto deviceQuota = core::makeShared<core::CpuMemoryQuota>(composition.maxCpuMetadataBytes);
    auto next =
        webgl::WebGlDevice::create(std::move(resources), composition, deviceQuota, diagnostic);
    REQUIRE(next, diagnostic);
    // The original immutable publication is the explicit retained reload recipe.
    // The old layout/pipeline cannot be imported into this new resource registry.
    auto layout = core::shareUnique(next->createPipelineLayout(pipeline->layout()->desc()));
    auto* original = dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get());
    REQUIRE(layout
            && next->selectPipelineInputs(original->impl->layout, artifact, original->impl->request)
            && !next->selectPipelineInputs(layout, artifact, original->impl->request),
        "RECOVERY-ONLY-NEW-ORIGINAL-LAYOUT-ADMITTED");
    state = RenderPipelineStateCopy::copy(original->impl->definition);
    state.shaderPackage = nullptr;
    state.layout = layout.get();
    auto rebuilt = core::shareUnique(next->createRenderPipeline(state));
    REQUIRE(rebuilt, next->diagnostic());
    // Retire old native numeric handles while the new context/pipeline is live.
    commands.reset();
    pass.reset();
    encoder.reset();
    target.reset();
    canvas.reset();
    image.texture.reset();
    canvasPipeline.reset();
    descriptors.reset();
    arena.reset();
    buffer.reset();
    REQUIRE(!commandQuota->liveBytes(), "RECOVERY-OLD-CPU-COMMAND-CUSTODY-RELEASED");
    restoredCaptureGate();
    REQUIRE(execute(*next, *originalResources, rebuilt), failure);
    recovered = true;
    // A second synthetic loss cannot silently replenish the declared one-attempt budget.
    REQUIRE(syntheticLossNow() && syntheticRestoreAfterLoss(), "RECOVERY-SECOND-SYNTHETIC-LOSS");
    auto exhaustedMetadata =
        core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuMetadataBytes);
    auto exhaustedUploads =
        core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes);
    const bool fault = recoveryFaultMode();
    if (fault)
        faultOnRecoveryQuery();
    REQUIRE(!next->restoreResources(ownership, exhaustedMetadata, exhaustedUploads, diagnostic)
            && diagnostic
            && std::string_view(diagnostic)
                == (fault ? "RHI-WEBGL1-CONTEXT-PROBE-NATIVE-ERROR"
                          : "RHI-WEBGL1-RECOVERY-BUDGET-EXHAUSTED")
            && !exhaustedMetadata->liveBytes() && !exhaustedUploads->liveBytes()
            && next->contextPhase()
                == (fault ? WebGlContextPhase::Failed : WebGlContextPhase::RestorePending)
            && !next->createBuffer(bufferDesc),
        "RECOVERY-ATTEMPT-BUDGET-OR-REQUERY-FAILS-CLOSED");
    REQUIRE(!next->restoreResources(ownership, exhaustedMetadata, exhaustedUploads, diagnostic)
            && !exhaustedMetadata->liveBytes() && !exhaustedUploads->liveBytes()
            && next->contextPhase()
                == (fault ? WebGlContextPhase::Failed : WebGlContextPhase::RestorePending),
        "RECOVERY-FAILURE-IS-STICKY-AND-DOES-NOT-LEAK-CANDIDATE-OWNERS");
    const auto retired = next->retireCpu({1, true});
    REQUIRE(retired.success && retired.admissionClosed && !retired.gpuCompletionKnown
            && arp::rhi::tests::WebGlCpuRetirementSmoke::rejectedDrain(*next)
            && next->retireCpu({1, true}).success,
        "RECOVERY-FAILED-OR-LOST-CPU-RETIREMENT-WITHOUT-FAKE-DRAIN");
    rebuilt.reset();
    layout.reset();
    next.reset();
    REQUIRE(!deviceQuota->liveBytes() && !metadata->liveBytes() && !uploads->liveBytes(),
        "RECOVERY-NEW-DOMAIN-EXACT-RETIREMENT");
    return true;
}

bool PipelineSmoke::multipass(
    webgl::WebGlDevice& device, const std::shared_ptr<RenderPipeline>& pipeline)
{
    GpuDevice& gpu = device;
    TextureDesc shape;
    shape.extent = {32, 32, 1};
    shape.format = TextureFormat::Rgba8Unorm;
    shape.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled | TextureUsage::CopyDst;
    shape.initialState = ResourceState::ColorAttachment;
    auto source = gpu.createTexture(shape);
    auto output = gpu.createTexture(shape);
    shape.format = TextureFormat::Depth24Stencil8;
    shape.usage = TextureUsage::RenderAttachment;
    shape.initialState = ResourceState::DepthAttachment;
    auto depth = gpu.createTexture(shape);
    REQUIRE(source && output && depth, device.diagnostic());
    const std::array<std::shared_ptr<webgl::WebGlFramebuffer>, 2> targets{
        core::shareUnique(device.createFramebuffer(*source, depth.get())),
        core::shareUnique(device.createFramebuffer(*output, depth.get()))};
    REQUIRE(targets[0] && targets[1], device.diagnostic());
    TextureViewDesc vd;
    vd.format = TextureFormat::Rgba8Unorm;
    vd.usage = TextureUsage::Sampled;
    auto view = gpu.createTextureView(*source, vd);
    SamplerDesc sd;
    sd.minFilter = sd.magFilter = SamplerFilter::Nearest;
    sd.mipFilter = SamplerMipFilter::None;
    auto sampler = gpu.createSampler(sd);
    BufferDesc bd;
    bd.sizeBytes = 128;
    bd.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    bd.memory = MemoryUsage::CpuToGpu;
    bd.initialState = ResourceState::ShaderRead;
    auto uniform = gpu.createBuffer(bd);
    bd.sizeBytes = 27 * sizeof(float);
    bd.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
    auto vertex = gpu.createBuffer(bd);
    const std::array<float, 27> vertices{-1,
        -1,
        .25f,
        .5f,
        .5f,
        1,
        1,
        1,
        1,
        -1,
        3,
        .25f,
        .5f,
        .5f,
        1,
        1,
        1,
        1,
        3,
        -1,
        .25f,
        .5f,
        .5f,
        1,
        1,
        1,
        1};
    std::array<float, 32> constants{};
    for (std::size_t i : {0u, 5u, 10u, 15u, 16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u})
        constants[i] = 1;
    REQUIRE(view && sampler && uniform && vertex
            && gpu.writeBuffer(*uniform, constants.data(), sizeof(constants), 0).success
            && gpu.writeBuffer(*vertex, vertices.data(), sizeof(vertices), 0).success,
        device.diagnostic());
    auto arena = gpu.createDescriptorArena({{}, 1, 5});
    REQUIRE(arena, device.diagnostic());
    DescriptorSetDesc setDesc;
    setDesc.layout = pipeline->layout()->desc().descriptorLayouts[0];
    auto descriptors = arena->allocateDescriptorSet(setDesc);
    REQUIRE(descriptors, device.diagnostic());
    DescriptorBindingWrite write;
    write.resource = uniform->id();
    write.type = DescriptorType::UniformBuffer;
    write.resourceKind = DescriptorResourceKind::Buffer;
    REQUIRE(descriptors->write(write).success, "MULTIPASS-UNIFORM-BINDING");
    for (std::uint32_t binding : {1u, 3u}) {
        write.binding = binding;
        write.resource = view->id();
        write.type = DescriptorType::SampledTexture;
        write.resourceKind = DescriptorResourceKind::TextureView;
        REQUIRE(descriptors->write(write).success, "MULTIPASS-TEXTURE-BINDING");
        write.binding = binding + 1;
        write.resource = sampler->id();
        write.type = DescriptorType::Sampler;
        write.resourceKind = DescriptorResourceKind::Sampler;
        REQUIRE(descriptors->write(write).success, "MULTIPASS-SAMPLER-BINDING");
    }
    WebGlPreparedCommandsRequest request;
    request.contractVersion = 3;
    request.prototypeOptIn = true;
    request.maxDraws = 1;
    request.maxUniformBytes = 160;
    request.maxAnalysisIndices = 3;
    request.maxPipelines = 1;
    request.maxTargets = request.maxPasses = 2;
    request.maxTransitions = 4;
    request.issue = {1, true, 5, 16, 24, 256 * 1024, 20};
    auto quota = core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
    const std::array<std::shared_ptr<RenderPipeline>, 1> pipelines{pipeline};
    RenderPassDesc first;
    first.colorAttachments.push_back({{},
        source->id(),
        TextureFormat::Rgba8Unorm,
        LoadOp::Clear,
        StoreOp::Store,
        false,
        {},
        {1, 0, 1, 1}});
    first.hasDepthStencilAttachment = true;
    first.depthStencilAttachment.target = depth->id();
    first.depthStencilAttachment.format = TextureFormat::Depth24Stencil8;
    first.depthStencilAttachment.depthLoadOp = first.depthStencilAttachment.stencilLoadOp =
        LoadOp::Clear;
    auto second = first;
    second.colorAttachments[0].target = output->id();
    second.colorAttachments[0].clearColor = {0, 0, 1, 1};
    ResourceBarrierDesc barrier;
    barrier.resource = source->id();
    barrier.textureSubresources = defaultTextureSubresourceRange(source->desc());
    barrier.before = ResourceState::ColorAttachment;
    barrier.after = ResourceState::ShaderRead;
    barrier.sourceStages = PipelineStage::ColorAttachmentOutput;
    barrier.sourceAccess = ResourceAccess::ColorAttachmentWrite;
    barrier.destinationStages = PipelineStage::FragmentShader;
    barrier.destinationAccess = ResourceAccess::ShaderRead;
    const std::array<float, 8> push{0, 0, 0, 0, 1, 1, 1, 0};
    auto* original = dynamic_cast<webgl::WebGlTexture*>(source.get());
    const auto prepare = [&]() {
        if (device.prepareCommand(pipelines, targets, request, quota))
            return core::UniquePtr<CommandEncoder>{};
        return gpu.createCommandEncoder({});
    };
    const auto bind = [&](RenderPassEncoder& pass) {
        return pass.setPipeline(*pipeline).success && pass.setViewport({0, 0, 32, 32, 0, 1}).success
            && pass.setScissor({0, 0, 32, 32}).success && pass.bindVertexBuffer(3, *vertex).success
            && pass.bindDescriptorSet(0, *descriptors).success
            && pass.pushConstants(
                       ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data())
                   .success;
    };
    // Abandoning a finished command must restore the original state as well.
    {
        auto encoder = prepare();
        REQUIRE(
            encoder && encoder->transition(*source, barrier).success, "MULTIPASS-ROLLBACK-RECORD");
        const std::array<std::uint8_t, 4> pendingPixel{255, 0, 255, 255};
        WebGlColdTextureUpload pendingUpload;
        pendingUpload.contractVersion = 1;
        pendingUpload.extent = {1, 1, 1};
        pendingUpload.bytesPerRow = 4;
        pendingUpload.bytes = pendingPixel;
        REQUIRE(!device.uploadTexture(*source, pendingUpload).success,
            "MULTIPASS-PENDING-TRANSITION-REJECTS-UPLOAD");
        auto pass = encoder->beginRenderPass(second);
        REQUIRE(pass && pass->end().success, "MULTIPASS-ROLLBACK-PASS");
        auto commands = encoder->finish();
        REQUIRE(
            commands && source->state() == ResourceState::ShaderRead, "MULTIPASS-PENDING-STATE");
    }
    REQUIRE(source->state() == ResourceState::ColorAttachment && !quota->liveBytes(),
        "MULTIPASS-ABANDON-ROLLBACK");
    // Both invalid recordings fail before native execution and are abandoned.
    for (unsigned mode = 0; mode < 2; ++mode) {
        auto encoder = prepare();
        first.colorAttachments[0].storeOp = mode ? StoreOp::DontCare : StoreOp::Store;
        auto a = encoder->beginRenderPass(first);
        REQUIRE(a && a->end().success, "MULTIPASS-NEGATIVE-FIRST");
        if (mode)
            REQUIRE(encoder->transition(*source, barrier).success, "MULTIPASS-DONTCARE-TRANSITION");
        auto b = encoder->beginRenderPass(second);
        REQUIRE(b && bind(*b) && !b->draw(3, 1, 0, 0).success,
            "MULTIPASS-MISSING-STATE-OR-CONTENT-REJECTED");
    }
    first.colorAttachments[0].storeOp = StoreOp::Store;
    REQUIRE(source->state() == ResourceState::ColorAttachment && !quota->liveBytes(),
        "MULTIPASS-NEGATIVE-ROLLBACK");
    auto encoder = prepare();
    REQUIRE(encoder, device.diagnostic());
    ResourceBarrierDesc bufferBarrier;
    bufferBarrier.resource = vertex->id();
    bufferBarrier.before = ResourceState::ShaderRead;
    bufferBarrier.after = ResourceState::CopyDst;
    bufferBarrier.sourceStages = PipelineStage::VertexInput;
    bufferBarrier.sourceAccess = ResourceAccess::VertexAttributeRead;
    bufferBarrier.destinationStages = PipelineStage::Transfer;
    bufferBarrier.destinationAccess = ResourceAccess::TransferWrite;
    auto bufferReady = bufferBarrier;
    bufferReady.before = bufferBarrier.after;
    bufferReady.after = bufferBarrier.before;
    bufferReady.sourceStages = bufferBarrier.destinationStages;
    bufferReady.sourceAccess = bufferBarrier.destinationAccess;
    bufferReady.destinationStages = bufferBarrier.sourceStages;
    bufferReady.destinationAccess = bufferBarrier.sourceAccess;
    const auto before = core::defaultCpuAllocator().stats().allocationCount;
    REQUIRE(encoder->transition(*vertex, bufferBarrier).success
            && encoder->transition(*vertex, bufferReady).success,
        "MULTIPASS-BUFFER-STATE-SEQUENCE");
    auto a = encoder->beginRenderPass(first);
    REQUIRE(a && a->end().success && encoder->transition(*source, barrier).success,
        "MULTIPASS-OFFSCREEN-TRANSITION");
    auto b = encoder->beginRenderPass(second);
    REQUIRE(b, encoder->lastValidation().diagnostics.c_str());
    REQUIRE(bind(*b), "MULTIPASS-BIND");
    const auto sampledDraw = b->draw(3, 1, 0, 0);
    REQUIRE(sampledDraw.success, sampledDraw.diagnostics.c_str());
    REQUIRE(
        b->end().success && encoder->transition(*vertex, bufferBarrier).success, "MULTIPASS-END");
    auto commands = encoder->finish();
    REQUIRE(commands && core::defaultCpuAllocator().stats().allocationCount == before,
        "MULTIPASS-NO-HOT-ALLOCATION");
    REQUIRE(!a->setScissor({0, 0, 32, 32}).success, "MULTIPASS-OLD-PASS-HANDLE-REJECTED");
    ++original->payload->revision; // Test-only later-input staleness.
    const auto stale = gpu.queue().submit(*commands);
    REQUIRE(
        !stale.success && !stale.hasAcceptedWork(), "MULTIPASS-ALL-PASSES-PREFLIGHT-BEFORE-NATIVE");
    --original->payload->revision;
    const auto accepted = gpu.queue().submit(*commands);
    REQUIRE(accepted.success && accepted.submittedCommandCount == commands->commandCount(),
        "MULTIPASS-REAL-QUEUE");
    REQUIRE(source->state() == ResourceState::ShaderRead && original->payload->gpuColorInitialized
            && !original->payload->state.pending(),
        "MULTIPASS-STATE-OWNERSHIP-COMMITTED");
    auto* result = dynamic_cast<webgl::WebGlTexture*>(output.get());
    glBindFramebuffer(GL_FRAMEBUFFER, result->payload->domain->find(targets[1]->id())->native);
    std::array<std::uint8_t, 32 * 32 * 4> pixels{};
    glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    REQUIRE(glGetError() == GL_NO_ERROR, "MULTIPASS-NATIVE-READBACK");
    for (std::size_t i = 0; i < pixels.size(); i += 4)
        REQUIRE(
            pixels[i] == 255 && pixels[i + 1] == 0 && pixels[i + 2] == 255 && pixels[i + 3] == 255,
            "MULTIPASS-ALL-PIXELS-OFFSCREEN-MAGENTA");
    commands.reset();
    a.reset();
    b.reset();
    encoder.reset();
    REQUIRE(!quota->liveBytes(), "MULTIPASS-COMMAND-RETIREMENT");
    // Default drawing buffer is a real leased image, never a sampled texture.
    SwapchainDesc canvasDesc;
    canvasDesc.surfaceName = "#canvas";
    canvasDesc.extent = {64, 64};
    canvasDesc.format = TextureFormat::Rgba8Unorm;
    canvasDesc.imageCount = 0;
    {
        auto discarded = gpu.createSwapchain(canvasDesc);
        REQUIRE(discarded, device.diagnostic());
        const auto acquired = discarded->acquireNextFrame();
        REQUIRE(acquired.success && discarded->discardFrame(acquired).success
                && !discarded->discardFrame(acquired).success
                && !discarded->acquireNextFrame().success,
            "CANVAS-UNSUBMITTED-DISCARD-SINGLE-USE");
    }
    {
        auto discarded = gpu.createSwapchain(canvasDesc);
        REQUIRE(discarded && !device.enableCanvasReuse(*discarded, {1, true, 2}),
            "CANVAS-REUSE-DISCARD-SETUP");
        auto frame = discarded->acquireNextFrame();
        auto returned = discarded->acquiredImage(frame);
        REQUIRE(returned.success && discarded->discardFrame(frame).success
                && !device.recycleCanvas(*discarded, frame, returned.texture),
            "CANVAS-REUSE-UNSUBMITTED-DISCARD-RETURN");
        auto next = discarded->acquireNextFrame();
        REQUIRE(next.success && next.frameSerial != frame.frameSerial
                && !discarded->isFrameAcquired(frame) && discarded->discardFrame(next).success
                && device.recycleCanvas(*discarded, next, returned.texture),
            "CANVAS-REUSE-DISCARD-WITHOUT-IMAGE-BUDGET");
    }
    auto canvas = gpu.createSwapchain(canvasDesc);
    REQUIRE(canvas && !gpu.createSwapchain(canvasDesc), "CANVAS-EXCLUSIVE-ORIGINAL-OWNER");
    // Fixture workload declares eight sequential leases; production has no frame default.
    constexpr std::uint32_t frameCount = 8;
    REQUIRE(!device.enableCanvasReuse(*canvas, {1, true, frameCount})
            && device.enableCanvasReuse(*canvas, {1, true, frameCount}),
        "CANVAS-REUSE-EXPLICIT-ONCE");
    const auto beforeAcquire = core::defaultCpuAllocator().stats().allocationCount;
    auto acquired = canvas->acquireNextFrame();
    auto image = canvas->acquiredImage(acquired);
    REQUIRE(acquired.success && image.success && image.texture
            && core::defaultCpuAllocator().stats().allocationCount == beforeAcquire,
        "CANVAS-COLD-IMAGE-ALLOCATION-FREE-ACQUIRE");
    REQUIRE(!canvas->acquireNextFrame().success && !canvas->acquiredImage(acquired).success
            && !canvas->present(acquired).success,
        "CANVAS-ONE-SHOT-UNSUBMITTED-PRESENT-REJECTED");
    auto forged = acquired;
    ++forged.frameSerial;
    REQUIRE(!canvas->isFrameAcquired(forged) && !device.canvasFramebuffer(*canvas, forged)
            && !gpu.createTextureView(*image.texture, vd),
        "CANVAS-ORIGINAL-LEASE-NOT-SAMPLED-TEXTURE");
    auto canvasTarget = device.canvasFramebuffer(*canvas, acquired);
    REQUIRE(canvasTarget, device.diagnostic());
    auto canvasState = RenderPipelineStateCopy::copy(
        dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get())->impl->definition);
    canvasState.shaderPackage = nullptr;
    canvasState.depthFormat = canvasState.depthStencil.format = TextureFormat::Unknown;
    auto canvasPipeline = core::shareUnique(gpu.createRenderPipeline(canvasState));
    REQUIRE(canvasPipeline, device.diagnostic());
    const std::array<std::shared_ptr<RenderPipeline>, 1> canvasPipelines{canvasPipeline};
    REQUIRE(
        !device.prepareCommand(canvasPipelines, canvasTarget, request, quota), device.diagnostic());
    auto canvasEncoder = gpu.createCommandEncoder({});
    REQUIRE(canvasEncoder && canvasEncoder->transition(*vertex, bufferReady).success,
        "CANVAS-VERTEX-STATE");
    REQUIRE(!gpu.writeBuffer(*vertex, vertices.data(), sizeof(vertices), 0).success,
        "CANVAS-PENDING-TRANSITION-REJECTS-BUFFER-WRITE");
    RenderPassDesc canvasPassDesc;
    canvasPassDesc.colorAttachments.push_back({{},
        image.ref.image,
        TextureFormat::Rgba8Unorm,
        LoadOp::Load,
        StoreOp::Store,
        false,
        {},
        {0, 0, 1, 1}});
    REQUIRE(
        !canvasEncoder->beginRenderPass(canvasPassDesc), "CANVAS-PREVIOUS-CONTENTS-LOAD-REJECTED");
    canvasPassDesc.colorAttachments[0].loadOp = LoadOp::Clear;
    const auto beforeCanvas = core::defaultCpuAllocator().stats().allocationCount;
    auto canvasPass = canvasEncoder->beginRenderPass(canvasPassDesc);
    REQUIRE(canvasPass && canvasPass->setPipeline(*canvasPipeline).success
            && canvasPass->setViewport({0, 0, 64, 64, 0, 1}).success
            && canvasPass->setScissor({0, 0, 64, 64}).success
            && canvasPass->bindVertexBuffer(3, *vertex).success
            && canvasPass->bindDescriptorSet(0, *descriptors).success
            && canvasPass
                ->pushConstants(
                    ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data())
                .success
            && canvasPass->draw(3, 1, 0, 0).success && canvasPass->end().success,
        "CANVAS-ORIGINAL-SAMPLED-DRAW");
    auto canvasCommands = canvasEncoder->finish();
    REQUIRE(canvasCommands && core::defaultCpuAllocator().stats().allocationCount == beforeCanvas,
        "CANVAS-RECORD-FINISH-NO-HOT-ALLOCATION");
    REQUIRE(!canvas->resize({32, 64}).success && !canvas->reconfigure({}).success,
        "CANVAS-LEASED-LIFECYCLE-MUTATION-REJECTED");
    resizeCanvasForOracle(32);
    const auto resized = gpu.queue().submit(*canvasCommands);
    REQUIRE(!resized.success && !resized.hasAcceptedWork() && !canvas->isFrameAcquired(acquired),
        "CANVAS-EXTERNAL-RESIZE-PREFLIGHT-BEFORE-ISSUE");
    resizeCanvasForOracle(64);
    REQUIRE(gpu.queue().submit(*canvasCommands).success, "CANVAS-DEFAULT-FRAMEBUFFER-EXECUTION");
    REQUIRE(!canvas->discardFrame(acquired).success, "CANVAS-SUBMITTED-DISCARD-REJECTED");
    std::array<std::uint8_t, 64 * 64 * 4> canvasPixels{};
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, canvasPixels.data());
    REQUIRE(glGetError() == GL_NO_ERROR, "CANVAS-ORACLE-READBACK");
    for (std::size_t i = 0; i < canvasPixels.size(); i += 4)
        REQUIRE(canvasPixels[i] == 255 && canvasPixels[i + 1] == 0 && canvasPixels[i + 2] == 255
                && canvasPixels[i + 3] == 255,
            "CANVAS-DEFAULT-FRAMEBUFFER-ALL-PIXELS");
    const auto presented = canvas->present(acquired);
    REQUIRE(presented.success && image.texture->state() == ResourceState::Present
            && !canvas->isFrameAcquired(acquired) && !canvas->present(acquired).success
            && !canvas->acquireNextFrame().success,
        "CANVAS-BROWSER-HANDOFF-NOT-GPU-COMPLETION");
    REQUIRE(device.recycleCanvas(*canvas, acquired, image.texture) && image.texture,
        "CANVAS-REUSE-RETAINED-COMMAND-REJECTED");
    canvasCommands.reset();
    canvasPass.reset();
    canvasEncoder.reset();
    canvasTarget.reset();
    REQUIRE(!quota->liveBytes(), "CANVAS-REUSE-COLD-COMMAND-QUOTA-RETURNED");
    const auto stableCpu = core::defaultCpuAllocator().stats().liveBytes;
    const auto stableResources = gpu.memoryStats();
    repeatedFrames = 1;
    double lastBrowserFrame = nextBrowserFrame();
    ++browserFrames;
    // Reuse the larger readback oracle buffer; do not add a second Wasm stack image.
    core::ArrayView<std::uint8_t> updatePixels{canvasPixels.data(), 32 * 33 * 4};
    WebGlColdTextureUpload update;
    update.contractVersion = 1;
    update.extent = {32, 32, 1};
    update.bytesPerRow = 33 * 4;
    update.bytes = updatePixels;
    for (std::uint32_t frame = 1; frame < frameCount; ++frame) {
        REQUIRE(device.recycleCanvas(*canvas, acquired, source) && source && image.texture,
            "CANVAS-REUSE-FOREIGN-IMAGE-NOT-CONSUMED");
        const auto old = acquired;
        const auto beforeRecycle = core::defaultCpuAllocator().stats().allocationCount;
        REQUIRE(!device.recycleCanvas(*canvas, acquired, image.texture) && !image.texture,
            "CANVAS-REUSE-EXACT-IMAGE-RETURN");
        acquired = canvas->acquireNextFrame();
        image = canvas->acquiredImage(acquired);
        REQUIRE(acquired.success && acquired.frameSerial != old.frameSerial && image.success
                && core::defaultCpuAllocator().stats().allocationCount == beforeRecycle,
            "CANVAS-REUSE-NO-ALLOCATION");
        REQUIRE(!canvas->isFrameAcquired(old) && !canvas->present(old).success
                && !canvas->discardFrame(old).success
                && device.recycleCanvas(*canvas, old, image.texture),
            "CANVAS-REUSE-STALE-SERIAL-REJECTED");
        const std::array<std::uint8_t, 4> expected = frame % 2
            ? std::array<std::uint8_t, 4>{0, 255, 255, 255}
            : std::array<std::uint8_t, 4>{255, 0, 255, 255};
        for (std::size_t i = 0; i < updatePixels.size(); ++i)
            canvasPixels[i] = expected[i % 4];
        webgl::test::poisonNativeState();
        webgl::test::ExecutionAudit updateAudit(device, "ordered-updates");
        const auto beforeUpdates = core::defaultCpuAllocator().stats().allocationCount;
        REQUIRE(device.uploadTexture(*source, update).success
                && gpu.writeBuffer(*uniform, constants.data(), sizeof(constants), 0).success
                && gpu.writeBuffer(*vertex, vertices.data(), sizeof(vertices), 0).success
                && core::defaultCpuAllocator().stats().allocationCount == beforeUpdates,
            "CANVAS-REUSE-BOUNDED-ORDERED-UPDATES-NO-ALLOCATION");
        REQUIRE(updateAudit.finish(), "QUALITY-UPDATE-CALLS-BYTES-NO-ALLOCATION");
        canvasTarget = device.canvasFramebuffer(*canvas, acquired);
        webgl::test::ExecutionAudit prepareAudit(device, "cold-prepare");
        REQUIRE(
            canvasTarget && !device.prepareCommand(canvasPipelines, canvasTarget, request, quota),
            device.diagnostic());
        canvasEncoder = gpu.createCommandEncoder({});
        REQUIRE(canvasEncoder, device.diagnostic());
        REQUIRE(prepareAudit.finish(true), "QUALITY-COLD-PREPARE-NO-GL-CREATION");
        webgl::test::ExecutionAudit recordAudit(device, "record-finish");
        const auto beforeRecord = core::defaultCpuAllocator().stats().allocationCount;
        canvasPass = canvasEncoder->beginRenderPass(canvasPassDesc);
        REQUIRE(canvasPass && canvasPass->setPipeline(*canvasPipeline).success
                && canvasPass->setViewport({0, 0, 64, 64, 0, 1}).success
                && canvasPass->setScissor({0, 0, 64, 64}).success
                && canvasPass->bindVertexBuffer(3, *vertex).success
                && canvasPass->bindDescriptorSet(0, *descriptors).success
                && canvasPass
                    ->pushConstants(
                        ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data())
                    .success
                && canvasPass->draw(3, 1, 0, 0).success && canvasPass->end().success,
            "CANVAS-REUSE-ORIGINAL-PIPELINE-RECORD");
        canvasCommands = canvasEncoder->finish();
        REQUIRE(
            canvasCommands && core::defaultCpuAllocator().stats().allocationCount == beforeRecord,
            "CANVAS-REUSE-RECORD-FINISH-NO-ALLOCATION");
        REQUIRE(recordAudit.finish(), "QUALITY-RECORD-ZERO-GL-AND-ALLOCATION");
        REQUIRE(!device.uploadTexture(*source, update).success
                && !gpu.writeBuffer(*vertex, vertices.data(), sizeof(vertices), 0).success,
            "CANVAS-REUSE-RECORDED-READS-REJECT-UPDATES");
        webgl::test::poisonNativeState();
        const auto beforeIssue = device.executionTelemetry().execution;
        webgl::test::ExecutionAudit issueAudit(device, "submit");
        REQUIRE(gpu.queue().submit(*canvasCommands).success, "CANVAS-REUSE-ISSUE-ONCE");
        REQUIRE(issueAudit.finish(), "QUALITY-ISSUE-EXACT-CALLS-BYTES-NO-ALLOCATION");
        webgl::test::ExecutionAudit rejectionAudit(device, "diagnostic-rejection");
        REQUIRE(!gpu.queue().submit(*canvasCommands).success && rejectionAudit.finish(true),
            "QUALITY-REJECTION-NO-NATIVE-EFFECTS");
        const auto afterIssue = device.executionTelemetry().execution;
        webgl::test::observeOutcome("submit-and-repeat", beforeIssue, afterIssue);
        REQUIRE(afterIssue.accepted == beforeIssue.accepted + 1
                && afterIssue.failed == beforeIssue.failed + 1
                && afterIssue.lost == beforeIssue.lost && afterIssue.draws == beforeIssue.draws + 1,
            "QUALITY-ACCEPTED-FAILED-COUNTERS");
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, canvasPixels.data());
        REQUIRE(glGetError() == GL_NO_ERROR, "CANVAS-REUSE-ORACLE-READBACK");
        for (std::size_t i = 0; i < canvasPixels.size(); ++i)
            REQUIRE(canvasPixels[i] == expected[i % 4], "CANVAS-REUSE-UPDATED-ALL-PIXELS");
        webgl::test::ExecutionAudit presentAudit(device, "present");
        REQUIRE(canvas->present(acquired).success
                && device.recycleCanvas(*canvas, acquired, image.texture),
            "CANVAS-REUSE-RETAINED-ISSUED-OWNER-REJECTED");
        REQUIRE(presentAudit.finish(), "QUALITY-PRESENT-EXACT-CALLS");
        canvasCommands.reset();
        canvasPass.reset();
        canvasEncoder.reset();
        canvasTarget.reset();
        REQUIRE(!quota->liveBytes() && core::defaultCpuAllocator().stats().liveBytes == stableCpu
                && gpu.memoryStats().liveResourceCount == stableResources.liveResourceCount
                && gpu.memoryStats().allocatedBytes == stableResources.allocatedBytes,
            "CANVAS-REUSE-STABLE-CPU-LOGICAL-RESOURCE-STORAGE");
        const double browserFrame = nextBrowserFrame();
        REQUIRE(browserFrame > lastBrowserFrame, "CANVAS-REUSE-ACTUAL-RAF-ADVANCES");
        lastBrowserFrame = browserFrame;
        ++browserFrames;
        ++repeatedFrames;
    }
    REQUIRE(device.recycleCanvas(*canvas, acquired, image.texture) && image.texture,
        "CANVAS-REUSE-EXACT-LEASE-BUDGET-EXHAUSTED");
    canvas.reset();
    image.texture.reset();
    canvasPipeline.reset();
    for (std::size_t i = 0; i < updatePixels.size(); ++i)
        canvasPixels[i] = std::array<std::uint8_t, 4>{255, 0, 255, 255}[i % 4];
    REQUIRE(
        device.uploadTexture(*source, update).success, "CANVAS-REUSE-RESTORE-DECLARED-BASELINE");
    REQUIRE(!quota->liveBytes(), "CANVAS-EXACT-COMMAND-RETIREMENT");
    // A separately retained sampled command must reject an attachment writer.
    auto reader = prepare();
    REQUIRE(reader && vertex->state() == ResourceState::ShaderRead, "MULTIPASS-BUFFER-NEXT-READER");
    b = reader->beginRenderPass(second);
    REQUIRE(b && bind(*b) && b->draw(3, 1, 0, 0).success && b->end().success,
        "MULTIPASS-EXTERNAL-READER");
    auto reading = reader->finish();
    auto writer = prepare();
    auto reverse = barrier;
    reverse.before = barrier.after;
    reverse.after = barrier.before;
    reverse.sourceStages = barrier.destinationStages;
    reverse.sourceAccess = barrier.destinationAccess;
    reverse.destinationStages = barrier.sourceStages;
    reverse.destinationAccess = barrier.sourceAccess;
    REQUIRE(writer && writer->transition(*source, reverse).success, "MULTIPASS-WRITER-TRANSITION");
    a = writer->beginRenderPass(first);
    REQUIRE(a && a->end().success, "MULTIPASS-WRITER-RECORD");
    auto writing = writer->finish();
    const auto rejected = gpu.queue().submit(*writing);
    REQUIRE(!rejected.success && !rejected.hasAcceptedWork()
            && std::string_view(rejected.diagnostics) == "RHI-WEBGL1-ATTACHMENT-RETAINED-READ",
        "MULTIPASS-EXTERNAL-READER-BLOCKS-WRITE");
    return true;
}

bool PipelineSmoke::execute(webgl::WebGlDevice& device,
    webgl::WebGlResources& resources,
    const std::shared_ptr<RenderPipeline>& pipeline)
{
    GpuDevice& frontend = device;
    auto variant = RenderPipelineStateCopy::copy(
        dynamic_cast<webgl::WebGlRenderPipeline*>(pipeline.get())->impl->definition);
    variant.shaderPackage = nullptr;
    variant.depthStencil.depthTestEnabled = variant.depthStencil.depthWriteEnabled = true;
    variant.depthStencil.depthCompare = CompareOp::Always;
    variant.colorTargets[0].blendEnabled = true;
    variant.colorTargets[0].colorBlend = {
        BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha, BlendOp::Add};
    variant.colorTargets[0].alphaBlend = {BlendFactor::One, BlendFactor::Zero, BlendOp::Add};
    auto alternateDraw = core::shareUnique(frontend.createRenderPipeline(variant));
    REQUIRE(alternateDraw, device.diagnostic());
    const std::array<std::shared_ptr<RenderPipeline>, 2> catalog{pipeline, alternateDraw};
    TextureDesc targetDesc;
    targetDesc.extent = {64, 64, 1};
    targetDesc.format = TextureFormat::Rgba8Unorm;
    targetDesc.usage = TextureUsage::RenderAttachment;
    targetDesc.initialState = ResourceState::ColorAttachment;
    auto color = frontend.createTexture(targetDesc);
    targetDesc.format = TextureFormat::Depth24Stencil8;
    targetDesc.initialState = ResourceState::DepthAttachment;
    auto depth = frontend.createTexture(targetDesc);
    REQUIRE(color && depth, resources.diagnostic());
    auto framebuffer = core::shareUnique(device.createFramebuffer(*color, depth.get()));
    REQUIRE(framebuffer, resources.diagnostic());
    targetDesc.extent = {1, 1, 1};
    targetDesc.format = TextureFormat::Rgba8Unorm;
    targetDesc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
    targetDesc.initialState = ResourceState::ShaderRead;
    auto texture = frontend.createTexture(targetDesc);
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    WebGlColdTextureUpload upload{1, 0, {0, 0, 0}, {1, 1, 1}, 4, white};
    REQUIRE(texture && device.uploadTexture(*texture, upload).success, resources.diagnostic());
    TextureViewDesc viewDesc;
    viewDesc.format = TextureFormat::Rgba8Unorm;
    viewDesc.usage = TextureUsage::Sampled;
    auto view = frontend.createTextureView(*texture, viewDesc);
    auto sampler = frontend.createSampler({});
    SamplerDesc nearest;
    nearest.minFilter = nearest.magFilter = SamplerFilter::Nearest;
    auto otherSampler = frontend.createSampler(nearest);
    REQUIRE(view && sampler && otherSampler, resources.diagnostic());
    BufferDesc bufferDesc;
    bufferDesc.sizeBytes = 128;
    bufferDesc.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    bufferDesc.memory = MemoryUsage::CpuToGpu;
    bufferDesc.initialState = ResourceState::ShaderRead;
    auto uniform = frontend.createBuffer(bufferDesc);
    bufferDesc.sizeBytes = 3 * 9 * sizeof(float);
    bufferDesc.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
    auto vertex = frontend.createBuffer(bufferDesc);
    bufferDesc.sizeBytes = 4 * sizeof(std::uint16_t);
    bufferDesc.usage = ResourceUsage::IndexBuffer | ResourceUsage::CopyDst;
    auto index = frontend.createBuffer(bufferDesc);
    REQUIRE(uniform && vertex && index, resources.diagnostic());
    const std::array<float, 27> vertices{-1,
        -1,
        .25f,
        .5f,
        .5f,
        1,
        1,
        1,
        1,
        -1,
        3,
        .25f,
        .5f,
        .5f,
        1,
        1,
        1,
        1,
        3,
        -1,
        .25f,
        .5f,
        .5f,
        1,
        1,
        1,
        1};
    const std::array<std::uint16_t, 4> indices{0, 1, 2, 9};
    REQUIRE(frontend.writeBuffer(*vertex, vertices.data(), sizeof(vertices), 0).success
            && frontend.writeBuffer(*index, indices.data(), sizeof(indices), 0).success,
        resources.diagnostic());
    auto arena = frontend.createDescriptorArena({{}, 1, 5});
    const auto& layout = pipeline->layout()->desc();
    DescriptorSetDesc setDesc;
    setDesc.layout = layout.descriptorLayouts[0];
    auto descriptors = arena->allocateDescriptorSet(setDesc);
    REQUIRE(descriptors, resources.diagnostic());
    DescriptorBindingWrite write;
    write.type = DescriptorType::UniformBuffer;
    write.resourceKind = DescriptorResourceKind::Buffer;
    write.resource = uniform->id();
    REQUIRE(descriptors->write(write).success, "EXECUTION-UNIFORM-WRITE");
    for (std::uint32_t binding : {1u, 3u}) {
        write.binding = binding;
        write.type = DescriptorType::SampledTexture;
        write.resourceKind = DescriptorResourceKind::TextureView;
        write.resource = view->id();
        REQUIRE(descriptors->write(write).success, "EXECUTION-TEXTURE-WRITE");
        write.binding = binding + 1;
        write.type = DescriptorType::Sampler;
        write.resourceKind = DescriptorResourceKind::Sampler;
        write.resource = sampler->id();
        REQUIRE(descriptors->write(write).success, "EXECUTION-SAMPLER-WRITE");
    }
    WebGlPreparedCommandsRequest request;
    request.contractVersion = 2;
    request.maxPipelines = 2;
    request.prototypeOptIn = true;
    request.maxDraws = 2;
    request.maxUniformBytes = 2 * 160;
    request.maxAnalysisIndices = 3;
    request.issue = {1, true, 4, 7, 16, 256 * 1024, 15};
    auto commandQuota = core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    variant.vertexBuffers[0].strideBytes += sizeof(float);
    auto wrongPipeline = core::shareUnique(frontend.createRenderPipeline(variant));
    REQUIRE(wrongPipeline, device.diagnostic());
    const std::array<std::shared_ptr<RenderPipeline>, 2> wrongCatalog{pipeline, wrongPipeline};
    REQUIRE(device.prepareCommand(wrongCatalog, framebuffer, request, commandQuota)
            && !commandQuota->liveBytes() && !frontend.createCommandEncoder({}),
        "DEVICE-CATALOG-ABI-MISMATCH-PREFLIGHT");
    auto prepare = [&]() {
        diagnostic = device.prepareCommand(catalog, framebuffer, request, commandQuota);
        if (diagnostic)
            return core::UniquePtr<CommandEncoder>{};
        CommandEncoderDesc unsupported;
        unsupported.maxBindlessPipelines = 1;
        if (frontend.createCommandEncoder(unsupported))
            return core::UniquePtr<CommandEncoder>{};
        auto result = frontend.createCommandEncoder({});
        if (frontend.createCommandEncoder({}))
            return core::UniquePtr<CommandEncoder>{};
        return result;
    };
    RenderPassDesc passDesc;
    passDesc.colorAttachments.push_back({{},
        color->id(),
        TextureFormat::Rgba8Unorm,
        LoadOp::Clear,
        StoreOp::Store,
        false,
        {},
        {0, 0, 1, 1}});
    passDesc.hasDepthStencilAttachment = true;
    passDesc.depthStencilAttachment.target = depth->id();
    passDesc.depthStencilAttachment.format = TextureFormat::Depth24Stencil8;
    passDesc.depthStencilAttachment.depthLoadOp = passDesc.depthStencilAttachment.stencilLoadOp =
        LoadOp::Clear;
    std::array<float, 32> budgetConstants{};
    for (std::size_t i : {0u, 5u, 10u, 15u})
        budgetConstants[i] = 1;
    std::array<float, 8> budgetPush{};
    REQUIRE(
        frontend.writeBuffer(*uniform, budgetConstants.data(), sizeof(budgetConstants), 0).success,
        "QUALITY-BUDGET-UNIFORM-SOURCE");
    webgl::test::beginNativeAudit("budget-fixture-init");
    const double auditInitialized = emscripten_get_now();
    REQUIRE(webgl::test::endNativeAudit(
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, emscripten_get_now() - auditInitialized, 0),
        "QUALITY-AUDIT-INITIALIZED");
    webgl::test::CommandBudgetSmoke budgetSmoke{
        device, catalog, framebuffer, passDesc, *vertex, *index, *descriptors, budgetPush};
    REQUIRE(budgetSmoke.run(), "QUALITY-ORIGINAL-WORKLOAD-EXACT-ONE-LESS-EXHAUSTED-OVERFLOW");
    auto encoder = prepare();
    REQUIRE(encoder, diagnostic);
    const auto allocationsBeforePass = core::defaultCpuAllocator().stats().allocationCount;
    auto pass = encoder->beginRenderPass(passDesc);
    REQUIRE(pass && core::defaultCpuAllocator().stats().allocationCount == allocationsBeforePass,
        "EXECUTION-PASS-NO-HOT-ALLOCATION");
    REQUIRE(
        pass && !pass->setPipeline(*wrongPipeline).success, "DEVICE-UNDECLARED-PIPELINE-REJECTED");
    wrongPipeline.reset();
    const auto allocationsBeforeState = core::defaultCpuAllocator().stats().allocationCount;
    REQUIRE(pass && pass->setPipeline(*pipeline).success
            && pass->setViewport({0, 0, 64, 64, 0, 1}).success
            && pass->setScissor({0, 0, 32, 64}).success
            && pass->bindVertexBuffer(3, *vertex).success
            && pass->bindIndexBuffer(*index, IndexFormat::Uint16).success
            && pass->bindDescriptorSet(0, *descriptors).success,
        "EXECUTION-RECORD-STATE");
    REQUIRE(core::defaultCpuAllocator().stats().allocationCount == allocationsBeforeState,
        "EXECUTION-STATE-NO-HOT-ALLOCATION");
    std::array<float, 32> constants{};
    for (std::size_t i : {0u, 5u, 10u, 15u})
        constants[i] = 1;
    constants[16] = constants[19] = 1;
    for (std::size_t i = 20; i < 24; ++i)
        constants[i] = 1;
    std::array<float, 8> push{0, 0, 0, 0, 1, 0, 0, 0};
    REQUIRE(frontend.writeBuffer(*uniform, constants.data(), sizeof(constants), 0).success
            && pass
                ->pushConstants(
                    ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data())
                .success,
        "EXECUTION-FIRST-CONSTANTS");
    // Index extrema prove actual vertex fetch; the invalid fourth index cannot
    // be accepted merely because the index byte range is inside its buffer.
    REQUIRE(!pass->drawIndexed(4, 1, 0, 0, 0).success, "EXECUTION-INDEX-EXTREMA-NEGATIVE");
    auto* indexSource = dynamic_cast<webgl::WebGlBuffer*>(index.get());
    indexSource->payload->retainedReads = UINT32_MAX; // Test-only exhaustion.
    REQUIRE(!pass->drawIndexed(3, 1, 0, 0, 0).success, "EXECUTION-RETAINED-READ-EXHAUSTION");
    indexSource->payload->retainedReads = 0;
    const auto beforeDraw = core::defaultCpuAllocator().stats().allocationCount;
    REQUIRE(pass->drawIndexed(3, 1, 0, 0, 0).success, "EXECUTION-FIRST-INDEXED-DRAW");
    REQUIRE(core::defaultCpuAllocator().stats().allocationCount == beforeDraw,
        "EXECUTION-DRAW-NO-HOT-ALLOCATION");
    REQUIRE(!frontend.writeBuffer(*vertex, vertices.data(), sizeof(vertices), 0).success,
        "EXECUTION-RETAINED-VERTEX-WRITE-REJECTED");
    constants[16] = 0;
    constants[17] = 1;
    push[4] = 0;
    push[5] = 1;
    REQUIRE(frontend.writeBuffer(*uniform, constants.data(), sizeof(constants), 0).success
            && pass
                ->pushConstants(
                    ShaderStage::Vertex | ShaderStage::Fragment, 0, sizeof(push), push.data())
                .success
            && pass->setScissor({32, 0, 32, 64}).success,
        "EXECUTION-SECOND-CONSTANTS");
    REQUIRE(pass->setPipeline(*alternateDraw).success, "EXECUTION-CATALOG-PIPELINE-SWITCH");
    // Same native texture with conflicting ES2 sampler state fails before a
    // draw snapshot is committed. Correcting the write makes recording usable.
    write.binding = 4;
    write.resource = otherSampler->id();
    REQUIRE(descriptors->write(write).success && !pass->draw(3, 1, 0, 0).success,
        "EXECUTION-SAMPLER-CONFLICT-NEGATIVE");
    write.resource = sampler->id();
    REQUIRE(descriptors->write(write).success && pass->draw(3, 1, 0, 0).success,
        "EXECUTION-SECOND-NONINDEXED-DRAW");
    // Restore A after recording B: immutable draw state must still issue B.
    REQUIRE(pass->setPipeline(*pipeline).success, "EXECUTION-RESTORE-PIPELINE-AFTER-SNAPSHOT");
    // Native errors are neither caused nor consumed by recording.
    REQUIRE(glGetError() == GL_NO_ERROR, "EXECUTION-RECORD-NATIVE-ERROR");
    constants.fill(0);
    REQUIRE(frontend.writeBuffer(*uniform, constants.data(), sizeof(constants), 0).success,
        "EXECUTION-MUTATE-UNIFORM-AFTER-DRAW");
    arena->reset();
    descriptors.reset();
    arena.reset();
    auto originalIndex = dynamic_cast<webgl::WebGlBuffer*>(index.get())->payload;
    uniform.reset();
    vertex.reset();
    index.reset();
    view.reset();
    texture.reset();
    sampler.reset();
    otherSampler.reset();
    REQUIRE(pass->end().success, "EXECUTION-END");
    auto commands = encoder->finish();
    REQUIRE(commands && !encoder->finish(), "EXECUTION-ONE-SHOT-FINISH");
    pass.reset();
    encoder.reset();
    ++originalIndex->revision; // Test-only stale native-source fault.
    const auto stale = webgl::WebGlExecution::submit(resources, *commands);
    REQUIRE(!stale.success && !stale.acceptedOperations && !stale.mayHaveNativeSideEffects
            && std::string_view(stale.diagnostic) == "RHI-WEBGL1-DRAW-INDEX-STALE",
        "EXECUTION-FULL-PREFLIGHT-BEFORE-CLEAR");
    GLint stillUnbound = -1;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &stillUnbound);
    REQUIRE(!stillUnbound, "EXECUTION-PREFLIGHT-NO-FRAMEBUFFER-SIDE-EFFECT");
    --originalIndex->revision;
    originalIndex.reset();
    REQUIRE(frontend.queue().role() == GpuQueueRole::Graphics && !frontend.queue().submitCount(),
        "EXECUTION-ORIGINAL-L2-QUEUE");
    const auto badSync = frontend.queue().submit(*commands, nullptr, 1, nullptr, 0, nullptr);
    REQUIRE(!badSync.success && !badSync.hasAcceptedWork() && !frontend.queue().submitCount(),
        "EXECUTION-L2-QUEUE-NATIVE-SYNC-REJECTED");
    const auto submitted = webgl::WebGlExecution::submit(resources, *commands);
    REQUIRE(submitted.success && submitted.acceptedOperations == 4
            && submitted.mayHaveNativeSideEffects && !submitted.gpuCompleted,
        submitted.diagnostic ? submitted.diagnostic : "EXECUTION-REAL-SUBMISSION");
    REQUIRE(
        glIsEnabled(GL_BLEND) && glIsEnabled(GL_DEPTH_TEST), "EXECUTION-FROZEN-PIPELINE-B-ISSUED");
    const auto repeated = webgl::WebGlExecution::submit(resources, *commands);
    REQUIRE(!repeated.success && !repeated.acceptedOperations && !repeated.mayHaveNativeSideEffects,
        "EXECUTION-ONE-SHOT-SUBMIT");
    auto* colorPayload = dynamic_cast<webgl::WebGlTexture*>(color.get());
    REQUIRE(colorPayload->payload->gpuColorInitialized && !colorPayload->payload->cpuContentCurrent
            && !colorPayload->payload->content->initialized(0, 64 * 64 * 4),
        "EXECUTION-NATIVE-INITIALIZATION-NOT-CPU-BYTES");
    // Test-only readback oracle; the production adapter exposes no readback or
    // completion capability. Every pixel is checked against the exact workload.
    GLint previous = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(
        GL_FRAMEBUFFER, colorPayload->payload->domain->find(framebuffer->id())->native);
    std::array<std::uint8_t, 64 * 64 * 4> pixels{};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, previous);
    REQUIRE(glGetError() == GL_NO_ERROR, "EXECUTION-ORACLE-READBACK");
    for (std::size_t y = 0; y < 64; ++y)
        for (std::size_t x = 0; x < 64; ++x) {
            const auto offset = (y * 64 + x) * 4;
            REQUIRE(pixels[offset] == (x < 32 ? 255 : 0) && pixels[offset + 1] == (x < 32 ? 0 : 255)
                    && pixels[offset + 2] == 0 && pixels[offset + 3] == 255,
                "EXECUTION-IMMUTABLE-TWO-DRAW-PIXELS");
        }
    commands.reset();
    // Store/DontCare invalidates later Load without synthesizing CPU contents.
    passDesc.colorAttachments[0].loadOp = LoadOp::Load;
    passDesc.colorAttachments[0].storeOp = StoreOp::DontCare;
    passDesc.depthStencilAttachment.depthLoadOp = passDesc.depthStencilAttachment.stencilLoadOp =
        LoadOp::Load;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        encoder = prepare();
        REQUIRE(encoder, diagnostic);
        pass = encoder->beginRenderPass(passDesc);
        REQUIRE(pass && pass->end().success, "EXECUTION-LOAD-RECORD");
        commands = encoder->finish();
        REQUIRE(commands, "EXECUTION-LOAD-FINISH");
        auto load = frontend.queue().submit(*commands);
        if (!attempt) {
            REQUIRE(load.success && load.submittedCommandCount == 2
                    && frontend.queue().submitCount() == 1,
                "EXECUTION-INITIALIZED-LOAD");
        } else {
            REQUIRE(!load.success && !load.submittedCommandCount && !load.hasAcceptedWork()
                    && frontend.queue().submitCount() == 1
                    && std::string_view(load.diagnostics)
                        == "RHI-WEBGL1-ATTACHMENT-LOAD-UNINITIALIZED",
                "EXECUTION-UNINITIALIZED-LOAD-NO-SIDE-EFFECT");
        }
        commands.reset();
        pass.reset();
        encoder.reset();
    }
    REQUIRE(multipass(device, pipeline), failure);
    passDesc.colorAttachments[0].loadOp = LoadOp::Clear;
    passDesc.depthStencilAttachment.depthLoadOp = passDesc.depthStencilAttachment.stencilLoadOp =
        LoadOp::Clear;
    encoder = prepare();
    REQUIRE(encoder, diagnostic);
    pass = encoder->beginRenderPass(passDesc);
    REQUIRE(pass && pass->end().success, "EXECUTION-FAULT-RECORD");
    commands = encoder->finish();
    REQUIRE(commands, "EXECUTION-FAULT-FINISH");
    const auto beforeFaultSubmissions = frontend.queue().submitCount();
    const auto beforePartial = device.executionTelemetry().execution;
    faultOnFinalUnbind();
    const auto partial = frontend.queue().submit(*commands);
    REQUIRE(!partial.success && partial.submittedCommandCount == 1 && partial.hasAcceptedWork()
            && frontend.queue().submitCount() == beforeFaultSubmissions + 1
            && !colorPayload->payload->nativeContentsValid && resources.currentDiagnostic(),
        "EXECUTION-NATIVE-PARTIAL-ACCEPTANCE-FAIL-CLOSED");
    const auto afterPartial = device.executionTelemetry().execution;
    webgl::test::observeOutcome("partial-native-failure", beforePartial, afterPartial);
    REQUIRE(afterPartial.failed == beforePartial.failed + 1
            && afterPartial.acceptedOperations == beforePartial.acceptedOperations + 1
            && afterPartial.nativeCalls > beforePartial.nativeCalls,
        "QUALITY-PARTIAL-FAILURE-RETAINS-EFFECTS");
    commands.reset();
    pass.reset();
    encoder.reset();
    framebuffer.reset();
    color.reset();
    depth.reset();
    REQUIRE(!commandQuota->liveBytes() && !resources.counters().logicalBufferBytes
            && !resources.counters().logicalTextureBytes,
        "EXECUTION-EXACT-RETIREMENT");
    return true;
}
} // namespace

int main()
{
    if (webgl::test::extensionExecutionMode()) {
        webgl::test::ExtensionExecutionSmoke extensions;
        const auto success = extensions.run(syntheticLossNow, syntheticRestoreAfterLoss);
        webgl::test::reportExtensionExecution(
            success, extensions.diagnostic.c_str(), extensions.negatives);
        return success ? 0 : 1;
    }
    PipelineSmoke test;
    const bool success = test.run();
    if (webgl::test::conformanceSampledMode()) {
        webgl::test::reportConformanceSampled(success,
            test.failure,
            test.conformanceHash.c_str(),
            test.orderedFrames,
            test.conformancePixels,
            test.conformanceSealed,
            test.conformanceAcceptedOnFailure,
            test.conformanceRetired,
            test.conformanceLost);
        return success ? 0 : 1;
    }
    if (webgl::test::conformanceClearMode()) {
        webgl::test::reportConformanceClear(success,
            test.failure,
            test.conformanceHash.c_str(),
            test.orderedFrames,
            test.conformancePixels,
            test.conformanceSealed,
            test.conformanceAcceptedOnFailure,
            test.conformanceRetired);
        return success ? 0 : 1;
    }
    if (webgl::test::basicForwardMode()) {
        webgl::test::reportBasicForward(success, test.failure, test.orderedFrames);
        return success ? 0 : 1;
    }
    if (webgl::test::guiMode()) {
        webgl::test::reportGui(success, test.failure, test.orderedFrames);
        return success ? 0 : 1;
    }
    if (webgl::test::scene2dMode()) {
        webgl::test::reportScene2D(success, test.failure, test.orderedFrames);
        return success ? 0 : 1;
    }
    if (webgl::test::orderedGraphicsMode()) {
        webgl::test::reportOrderedGraphics(success, test.failure, test.orderedFrames);
        return success ? 0 : 1;
    }
    reportPipeline(success,
        test.failure,
        test.variants,
        test.repeatedFrames,
        test.browserFrames,
        test.recovered,
        test.surfaceFrames,
        test.surfaceValidated,
        test.retirementValidated);
    return success ? 0 : 1;
}

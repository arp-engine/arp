#include "rhi/backends/webgl/webgl_runtime.h"

#include "shader/webgl_shader_limits.h"

#include <exception>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/threading.h>
#endif

namespace arp::rhi::webgl {
using namespace semantics;

struct WebGlRuntime::Impl {
    explicit Impl(std::shared_ptr<core::CpuMemoryQuota> q, std::string_view selector)
        : quota(std::move(q)), canvas(selector, &core::defaultCpuMemoryResource())
    {}

    ~Impl();
    std::shared_ptr<core::CpuMemoryQuota> quota;
    core::pmr::String canvas{&core::defaultCpuMemoryResource()};
    core::LifetimeEpoch lifetime;
    core::LifetimeEpoch surfaceLifetime;
    std::uint32_t maxSurfaceMutations = 0, surfaceMutations = 0;
    bool callerAvailable = true, documentHidden = false, pageHidden = false;
    Extent2D logicalSurfaceExtent;
    const char* surfaceFailure = nullptr;
    WebGlRuntimeProbe probe;
    std::shared_ptr<const WebGlRuntimeProfile> profile;
    WebGlContextOwnershipRequest creation;
    WebGlContextPhase phase = WebGlContextPhase::Ready;
    std::uint32_t maxRestorations = 0, restorationAttempts = 0;
    const char* recoveryFailure = nullptr;
    bool lost = false;
    bool shaderTextureLodEnabled = false;
#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    bool lossCallbackRegistered = false;
#endif
};

#ifdef __EMSCRIPTEN__
namespace {

// WebGlDeviceExecution/1 admits one main-thread context domain. Reserve the
// original owner before calling the browser, including synchronous callbacks.
WebGlRuntime::Impl* ownedRuntime = nullptr;

EM_JS_DEPS(webGlContextIdentity, "$lengthBytesUTF8,$stringToUTF8");
EM_JS_DEPS(webGlContextLoss, "$getWasmTableEntry");

EM_JS(int,
    registerLoss,
    (std::uintptr_t handle,
        void* user,
        std::uintptr_t callback,
        std::uintptr_t restoredCallback,
        std::uintptr_t visibilityCallback),
    {
        // clang-format off
        const context = GL.getContext(handle);
        if (!context || context.arpWebGlLossSubscription) return 0;
        const target = context.GLctx.canvas;
        const listener = event => { event.preventDefault(); getWasmTableEntry(callback)(user); };
        const restored = () => getWasmTableEntry(restoredCallback)(user);
        const documentTarget = target.ownerDocument;
        const windowTarget = documentTarget.defaultView;
        let pageHidden = false;
        const visibility = () => getWasmTableEntry(visibilityCallback)(user, documentTarget.hidden ? 1 : 0, pageHidden ? 1 : 0);
        const hide = () => { pageHidden = true; visibility(); };
        const show = () => { pageHidden = false; visibility(); };
        target.addEventListener('webglcontextlost', listener, false);
        target.addEventListener('webglcontextrestored', restored, false);
        documentTarget.addEventListener('visibilitychange', visibility, false);
        windowTarget.addEventListener('pagehide', hide, false);
        windowTarget.addEventListener('pageshow', show, false);
        context.arpWebGlLossSubscription = {target, listener, restored, documentTarget, windowTarget, visibility, hide, show};
        return 1;
        // clang-format on
    });

EM_JS(void, unregisterLoss, (std::uintptr_t handle), {
    // clang-format off
        const context = GL.getContext(handle);
        const binding = context && context.arpWebGlLossSubscription;
        if (binding) {
            // Retire the original DOM target; its authored selector may have changed.
            binding.target.removeEventListener('webglcontextlost', binding.listener, false);
            binding.target.removeEventListener('webglcontextrestored', binding.restored, false);
            binding.documentTarget.removeEventListener('visibilitychange', binding.visibility, false);
            binding.windowTarget.removeEventListener('pagehide', binding.hide, false);
            binding.windowTarget.removeEventListener('pageshow', binding.show, false);
            context.arpWebGlLossSubscription = null;
        }
    // clang-format on
});

EM_JS(int, identityText, (int field, char* destination, int capacity), {
    // clang-format off
        const gl = GL.currentContext && GL.currentContext.GLctx;
        if (!gl || gl.isContextLost()) return -1;
        const parameters = [gl.VERSION, gl.SHADING_LANGUAGE_VERSION, gl.VENDOR, gl.RENDERER];
        const value = field === 4 ? navigator.userAgent : gl.getParameter(parameters[field]);
        if (typeof value !== 'string') return -1;
        const bytes = lengthBytesUTF8(value);
        if (destination) {
            if (capacity <= bytes) return -1;
            stringToUTF8(value, destination, capacity);
        }
        return bytes;
    // clang-format on
});

EM_JS(int, extensionAvailable, (const char* name), {
    // clang-format off
        const gl = GL.currentContext && GL.currentContext.GLctx;
        const supported = gl && gl.getSupportedExtensions();
        return supported && supported.includes(UTF8ToString(name)) ? 1 : 0;
    // clang-format on
});

EM_JS(int, drawingBufferIsSrgb, (), {
    // clang-format off
        const gl = GL.currentContext && GL.currentContext.GLctx;
        return gl && gl.drawingBufferColorSpace === 'srgb' ? 1 : 0;
    // clang-format on
});

EM_JS(int, originalDocumentHidden, (), {
    // clang-format off
    return GL.currentContext.GLctx.canvas.ownerDocument.hidden ? 1 : 0;
    // clang-format on
});

EM_JS(int, resizeOriginalCanvas, (int width, int height), {
    // clang-format off
    const canvas = GL.currentContext.GLctx.canvas;
    try {
        canvas.width = width;
        canvas.height = height;
        return canvas.width === width && canvas.height === height ? 1 : 0;
    } catch (_) { return 0; }
    // clang-format on
});

EM_JS(GLenum, packedAttachment, (), {
    // clang-format off
        return GL.currentContext.GLctx.DEPTH_STENCIL_ATTACHMENT;
    // clang-format on
});

WebGlFormatFacts queryFormats(bool depthTextureEnabled, bool srgbEnabled)
{
    WebGlFormatFacts facts;
    facts.contractVersion = 2;
    const auto outcome = [](GLenum error) {
        if (emscripten_is_webgl_context_lost(emscripten_webgl_get_current_context()))
            return WebGlFormatQueryOutcome::ContextLost;
        return error == GL_NO_ERROR     ? WebGlFormatQueryOutcome::Observed
            : error == GL_OUT_OF_MEMORY ? WebGlFormatQueryOutcome::OutOfMemory
                                        : WebGlFormatQueryOutcome::NativeFailure;
    };
    GLuint texture = 0, framebuffer = 0, packed = 0;
    facts.rgba8Texture = outcome(glGetError());
    if (facts.rgba8Texture == WebGlFormatQueryOutcome::Observed) {
        glGenTextures(1, &texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Explicit cold query workload: one RGBA8 texel, not a resource-size promise.
        const std::array<GLubyte, 4> pixel{0, 0, 0, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        facts.rgba8Texture = outcome(glGetError());
        if (!texture)
            facts.rgba8Texture = WebGlFormatQueryOutcome::NativeFailure;
        facts.rgba8ClientUploadAccepted = facts.rgba8Texture == WebGlFormatQueryOutcome::Observed;
    }
    if (facts.rgba8ClientUploadAccepted) {
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        facts.rgba8FramebufferComplete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        facts.rgba8Framebuffer = outcome(glGetError());
        if (!framebuffer)
            facts.rgba8Framebuffer = WebGlFormatQueryOutcome::NativeFailure;
    }
    if (facts.rgba8Framebuffer == WebGlFormatQueryOutcome::Observed) {
        glGenRenderbuffers(1, &packed);
        glBindRenderbuffer(GL_RENDERBUFFER, packed);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_STENCIL_OES, 1, 1);
        GLint depth = 0, stencil = 0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &depth);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &stencil);
        facts.packedDepthBits = depth;
        facts.packedStencilBits = stencil;
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, packedAttachment(), GL_RENDERBUFFER, packed);
        facts.packedFramebufferComplete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        facts.packedDepthStencil = outcome(glGetError());
        if (!packed)
            facts.packedDepthStencil = WebGlFormatQueryOutcome::NativeFailure;
    }
    GLuint depthTexture = 0;
    if (depthTextureEnabled && facts.rgba8Framebuffer == WebGlFormatQueryOutcome::Observed) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, packedAttachment(), GL_RENDERBUFFER, 0);
        glGenTextures(1, &depthTexture);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_DEPTH_STENCIL_OES,
            1,
            1,
            0,
            GL_DEPTH_STENCIL_OES,
            GL_UNSIGNED_INT_24_8_OES,
            nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, packedAttachment(), GL_TEXTURE_2D, depthTexture, 0);
        facts.depthTextureFramebufferComplete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        facts.depthTexture = outcome(glGetError());
        if (!depthTexture)
            facts.depthTexture = WebGlFormatQueryOutcome::NativeFailure;
        glFramebufferTexture2D(GL_FRAMEBUFFER, packedAttachment(), GL_TEXTURE_2D, 0, 0);
    }
    if (depthTexture)
        glDeleteTextures(1, &depthTexture);
    if (framebuffer) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, packedAttachment(), GL_RENDERBUFFER, 0);
    }
    if (srgbEnabled) {
        GLuint srgb = 0;
        glGenTextures(1, &srgb);
        glBindTexture(GL_TEXTURE_2D, srgb);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_SRGB_ALPHA_EXT,
            1,
            1,
            0,
            GL_SRGB_ALPHA_EXT,
            GL_UNSIGNED_BYTE,
            nullptr);
        facts.srgbTexture = outcome(glGetError());
        if (!srgb)
            facts.srgbTexture = WebGlFormatQueryOutcome::NativeFailure;
        if (facts.srgbTexture == WebGlFormatQueryOutcome::Observed) {
            const std::array<GLubyte, 4> pixel{128, 128, 128, 255};
            glTexSubImage2D(
                GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_SRGB_ALPHA_EXT, GL_UNSIGNED_BYTE, pixel.data());
            facts.srgbClientUploadAccepted =
                outcome(glGetError()) == WebGlFormatQueryOutcome::Observed;
        }
        if (srgb)
            glDeleteTextures(1, &srgb);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    if (packed)
        glDeleteRenderbuffers(1, &packed);
    if (texture)
        glDeleteTextures(1, &texture);
    const auto cleanup = outcome(glGetError());
    if (cleanup != WebGlFormatQueryOutcome::Observed)
        facts.rgba8Texture = cleanup;
    return facts;
}

void contextLost(void* user)
{
    auto& runtime = *static_cast<WebGlRuntime::Impl*>(user);
    if (!runtime.lost) {
        runtime.lost = true;
        runtime.phase = WebGlContextPhase::Lost;
        runtime.lifetime.advance();
    }
    // A later browser restore must not revive this epoch or its resources.
}

void contextRestored(void* user)
{
    auto& runtime = *static_cast<WebGlRuntime::Impl*>(user);
    if (runtime.lost && runtime.phase == WebGlContextPhase::Lost)
        runtime.phase = WebGlContextPhase::RestorePending;
}

bool mutateSurface(WebGlRuntime::Impl& runtime) noexcept
{
    if (runtime.surfaceFailure)
        return false;
    if (runtime.surfaceMutations == runtime.maxSurfaceMutations) {
        runtime.surfaceFailure = "RHI-WEBGL1-SURFACE-MUTATION-BUDGET-EXHAUSTED";
        return false;
    }
    ++runtime.surfaceMutations;
    runtime.surfaceLifetime.advance();
    if (!runtime.surfaceLifetime.isActive()) {
        runtime.surfaceFailure = "RHI-WEBGL1-SURFACE-GENERATION-EXHAUSTED";
        return false;
    }
    return true;
}

void surfaceVisibility(void* user, int hidden, int pageHidden)
{
    auto& runtime = *static_cast<WebGlRuntime::Impl*>(user);
    if (!runtime.maxSurfaceMutations
        || (runtime.documentHidden == bool(hidden) && runtime.pageHidden == bool(pageHidden)))
        return;
    (void)mutateSurface(runtime);
    runtime.documentHidden = hidden;
    runtime.pageHidden = pageHidden;
}

const char* collect(WebGlRuntime::Impl& runtime, const WebGlContextOwnershipRequest& request)
{
    auto& probe = runtime.probe;
    probe = {};
    probe.contractVersion = kWebGl1GraphicsContractVersion;
    probe.probeVersion = kWebGlRuntimeProbeVersion;
    probe.contextAvailable = true;
    EmscriptenWebGLContextAttributes actual{};
    if (emscripten_webgl_get_context_attributes(runtime.context, &actual)
        != EMSCRIPTEN_RESULT_SUCCESS)
        return "RHI-WEBGL1-CONTEXT-ATTRIBUTES-QUERY";
    probe.contextMajorVersion = static_cast<std::uint32_t>(actual.majorVersion);
    probe.contextAttributes = {actual.alpha,
        actual.depth,
        actual.stencil,
        actual.antialias,
        actual.premultipliedAlpha,
        actual.preserveDrawingBuffer};
    probe.contextAttributesQueried = true;
    if (!probe.contextAttributes.matches(request.context.attributes))
        return "RHI-WEBGL1-CONTEXT-ATTRIBUTES-MISMATCH";
    if (!drawingBufferIsSrgb())
        return "RHI-WEBGL1-CONTEXT-COLOR-SPACE";
    for (std::size_t i = 0; i < webGlIdentityFieldCount; ++i) {
        const int bytes = identityText(static_cast<int>(i), nullptr, 0);
        if (bytes < 0 || static_cast<std::uint32_t>(bytes) >= request.maxIdentityFieldBytes)
            return "RHI-WEBGL1-CONTEXT-IDENTITY-BUDGET";
        auto& text = probe.identity.values[i];
        text.resize(static_cast<std::size_t>(bytes) + 1);
        if (identityText(static_cast<int>(i), text.data(), bytes + 1) != bytes)
            return "RHI-WEBGL1-CONTEXT-IDENTITY-QUERY";
        text.resize(static_cast<std::size_t>(bytes));
        probe.identity.queried[i] = true;
    }
    constexpr std::array names{GL_MAX_VERTEX_ATTRIBS,
        GL_MAX_VERTEX_UNIFORM_VECTORS,
        GL_MAX_FRAGMENT_UNIFORM_VECTORS,
        GL_MAX_VARYING_VECTORS,
        GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
        GL_MAX_TEXTURE_IMAGE_UNITS,
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        GL_MAX_TEXTURE_SIZE,
        GL_MAX_CUBE_MAP_TEXTURE_SIZE,
        GL_MAX_RENDERBUFFER_SIZE};
    const std::array fields{&probe.limits.maxVertexAttributes,
        &probe.limits.maxVertexUniformVectors,
        &probe.limits.maxFragmentUniformVectors,
        &probe.limits.maxVaryingVectors,
        &probe.limits.maxVertexTextureUnits,
        &probe.limits.maxFragmentTextureUnits,
        &probe.limits.maxCombinedTextureUnits,
        &probe.limits.maxTextureSize,
        &probe.limits.maxCubeTextureSize,
        &probe.limits.maxRenderbufferSize};
    for (std::size_t i = 0; i < names.size(); ++i) {
        GLint value = -1;
        glGetIntegerv(names[i], &value);
        if (value < 0)
            return "RHI-WEBGL1-LIMIT-PROBE-INVALID";
        *fields[i] = static_cast<std::uint32_t>(value);
    }
    std::array<GLint, 2> viewport{};
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport.data());
    if (viewport[0] <= 0 || viewport[1] <= 0)
        return "RHI-WEBGL1-LIMIT-PROBE-INVALID";
    probe.limits.maxViewportWidth = static_cast<std::uint32_t>(viewport[0]);
    probe.limits.maxViewportHeight = static_cast<std::uint32_t>(viewport[1]);
    probe.limitsQueried = true;
    constexpr std::array precisionTypes{
        GL_LOW_FLOAT, GL_MEDIUM_FLOAT, GL_HIGH_FLOAT, GL_LOW_INT, GL_MEDIUM_INT, GL_HIGH_INT};
    for (std::size_t stage = 0; stage < 2; ++stage)
        for (std::size_t i = 0; i < precisionTypes.size(); ++i) {
            GLint ranges[2]{}, precision = 0;
            glGetShaderPrecisionFormat(stage ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER,
                precisionTypes[i],
                ranges,
                &precision);
            auto& output = stage ? probe.precision.fragment[i] : probe.precision.vertex[i];
            output = {true, ranges[0], ranges[1], precision};
        }
    probe.precisionQueried = true;
    const std::array extensionNames{"OES_element_index_uint",
        "ANGLE_instanced_arrays",
        "OES_vertex_array_object",
        "WEBGL_depth_texture",
        "OES_standard_derivatives",
        "EXT_frag_depth",
        "EXT_sRGB"};
    constexpr std::array extensionFields{&WebGlRuntimeExtensions::uint32Indices,
        &WebGlRuntimeExtensions::instancing,
        &WebGlRuntimeExtensions::vertexArrayObjects,
        &WebGlRuntimeExtensions::depthTextures,
        &WebGlRuntimeExtensions::derivatives,
        &WebGlRuntimeExtensions::fragmentDepth,
        &WebGlRuntimeExtensions::srgbTextures};
    for (std::size_t i = 0; i < extensionNames.size(); ++i) {
        const auto member = extensionFields[i];
        probe.extensions.*member = extensionAvailable(extensionNames[i]) != 0;
        if (request.extensionOptIn.extensions.*member) {
            if (!(probe.extensions.*member))
                return "RHI-WEBGL1-EXTENSION-UNAVAILABLE";
            probe.enabledExtensions.*member =
                emscripten_webgl_enable_extension(runtime.context, extensionNames[i]);
            if (!(probe.enabledExtensions.*member))
                return "RHI-WEBGL1-EXTENSION-NOT-ENABLED";
        }
    }
    if (request.shaderTextureLodOptIn
        && (!extensionAvailable("EXT_shader_texture_lod")
            || !emscripten_webgl_enable_extension(runtime.context, "EXT_shader_texture_lod")))
        return "RHI-WEBGL1-EXTENSION-UNAVAILABLE";
    probe.extensionsQueried = true;
    probe.fragmentDepthQueryVersion = 1;
    runtime.shaderTextureLodEnabled = request.shaderTextureLodOptIn;
    if (glGetError() != GL_NO_ERROR)
        return "RHI-WEBGL1-CONTEXT-PROBE-NATIVE-ERROR";
    probe.formats =
        queryFormats(probe.enabledExtensions.depthTextures, probe.enabledExtensions.srgbTextures);
    probe.contextLost = emscripten_is_webgl_context_lost(runtime.context);
    auto next = core::makeShared<WebGlRuntimeProfile>(WebGlRuntimeProfile::fromProbe(probe));
    if (const auto* id = next->diagnostic())
        return id;
    runtime.profile = std::move(next);
    return nullptr;
}
} // namespace
#endif

WebGlRuntime::Impl::~Impl()
{
#ifdef __EMSCRIPTEN__
    if (ownedRuntime == this)
        ownedRuntime = nullptr;
    if (!context)
        return;
    // Only the main browser/runtime thread owns the context, even in products
    // with file workers. Proxy contexts and worker rendering remain forbidden.
    // Never issue GL/context destruction from a foreign worker thread.
    if (!emscripten_is_main_runtime_thread() || !emscripten_is_main_browser_thread())
        std::terminate();
    if (lossCallbackRegistered)
        unregisterLoss(context);
    lifetime.advance();
    emscripten_webgl_destroy_context(context);
#endif
}

WebGlRuntime::WebGlRuntime(ConstructionKey, core::UniquePtr<Impl> impl) : impl_(std::move(impl))
{}

WebGlRuntime::~WebGlRuntime() = default;

core::UniquePtr<WebGlRuntime> WebGlRuntime::create(const WebGlContextOwnershipRequest& request,
    const std::shared_ptr<core::CpuMemoryQuota>& quota,
    const char*& diagnostic)
{
    diagnostic = WebGlContextOwnershipContract::validateForBackend(Backend::WebGL, request);
    if (diagnostic)
        return {};
    if (!quota || quota->limitBytes() != request.maxCpuMetadataBytes) {
        diagnostic = "RHI-WEBGL1-CONTEXT-CPU-QUOTA";
        return {};
    }
#ifndef __EMSCRIPTEN__
    diagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
    return {};
#else
#ifdef __EMSCRIPTEN_PTHREADS__
    constexpr bool pthreadProduct = true;
#else
    constexpr bool pthreadProduct = false;
#endif
    diagnostic = WebGlContextOwnershipContract::executionThreadDiagnostic(request,
        pthreadProduct,
        emscripten_is_main_runtime_thread(),
        emscripten_is_main_browser_thread());
    if (diagnostic) {
        return {};
    }
    if (ownedRuntime) {
        diagnostic = "RHI-WEBGL1-CONTEXT-OWNER-ALREADY-ACTIVE";
        return {};
    }
    if (emscripten_webgl_get_current_context()) {
        diagnostic = "RHI-WEBGL1-CONTEXT-FOREIGN-CURRENT";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto impl = core::makeUnique<Impl>(quota, request.canvasSelector);
        impl->creation = request;
        impl->creation.canvasSelector = impl->canvas;
        ownedRuntime = impl.get();
        EmscriptenWebGLContextAttributes attributes{};
        const auto& intent = request.context.attributes;
        attributes.alpha = intent.alpha;
        attributes.depth = intent.depth;
        attributes.stencil = intent.stencil;
        attributes.antialias = intent.antialias;
        attributes.premultipliedAlpha = intent.premultipliedAlpha;
        attributes.preserveDrawingBuffer = intent.preserveDrawingBuffer;
        attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_DEFAULT;
        attributes.failIfMajorPerformanceCaveat = false;
        attributes.majorVersion = 1;
        attributes.minorVersion = 0;
        attributes.enableExtensionsByDefault = false;
        attributes.explicitSwapControl = false;
        attributes.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW;
        attributes.renderViaOffscreenBackBuffer = false;
        attributes.desynchronized = false;
        impl->context = emscripten_webgl_create_context(impl->canvas.c_str(), &attributes);
        if (!impl->context) {
            diagnostic = "RHI-WEBGL1-CONTEXT-CREATE";
            return {};
        }
        if (emscripten_webgl_make_context_current(impl->context) != EMSCRIPTEN_RESULT_SUCCESS) {
            diagnostic = "RHI-WEBGL1-CONTEXT-MAKE-CURRENT";
            return {};
        }
        diagnostic = collect(*impl, request);
        if (diagnostic)
            return {};
        if (!registerLoss(impl->context,
                impl.get(),
                reinterpret_cast<std::uintptr_t>(&contextLost),
                reinterpret_cast<std::uintptr_t>(&contextRestored),
                reinterpret_cast<std::uintptr_t>(&surfaceVisibility))) {
            diagnostic = "RHI-WEBGL1-CONTEXT-LOSS-REGISTRATION";
            return {};
        }
        impl->lossCallbackRegistered = true;
        return core::makeUnique<WebGlRuntime>(ConstructionKey{}, std::move(impl));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGL1-CONTEXT-METADATA-ALLOCATION";
        return {};
    }
#endif
}

const WebGlRuntimeProfile& WebGlRuntime::profile() const noexcept
{
    return *impl_->profile;
}

std::shared_ptr<const WebGlRuntimeProfile> WebGlRuntime::profileSnapshot() const noexcept
{
    return impl_->profile;
}

WebGlExtensionOptIn WebGlRuntime::extensionOptIn() const noexcept
{
    return impl_->creation.extensionOptIn;
}

core::LifetimeEpoch::Ticket WebGlRuntime::epoch() const noexcept
{
    return impl_->lifetime.ticket();
}

core::LifetimeEpoch::Ticket WebGlRuntime::surfaceEpoch() const noexcept
{
    return impl_->surfaceLifetime.ticket();
}

const char* WebGlRuntime::surfaceDiagnostic() const noexcept
{
    if (!impl_->maxSurfaceMutations)
        return nullptr;
    if (impl_->surfaceFailure)
        return impl_->surfaceFailure;
    if (!impl_->callerAvailable || impl_->documentHidden || impl_->pageHidden)
        return "RHI-WEBGL1-SURFACE-SUSPENDED";
    if (!impl_->logicalSurfaceExtent.width || !impl_->logicalSurfaceExtent.height)
        return "RHI-WEBGL1-SURFACE-ZERO-EXTENT";
    return nullptr;
}

const char* WebGlRuntime::enableSurfaceLifecycle(
    const WebGlSurfaceLifecycleRequest& request) noexcept
{
    if (const auto* id = WebGlSurfaceLifecycleContract::validate(Backend::WebGL, request))
        return id;
    if (const auto* id = currentDiagnostic())
        return id;
    if (impl_->maxSurfaceMutations)
        return "RHI-WEBGL1-SURFACE-LIFECYCLE-ALREADY-CONFIGURED";
    Extent2D extent;
    if (const auto* id = drawingBufferExtent(extent))
        return id;
    impl_->logicalSurfaceExtent = extent;
    impl_->maxSurfaceMutations = request.maxMutations;
#ifdef __EMSCRIPTEN__
    impl_->documentHidden = originalDocumentHidden();
#endif
    return nullptr;
}

const char* WebGlRuntime::setSurfaceAvailable(bool available) noexcept
{
    if (const auto* id = currentDiagnostic())
        return id;
    if (!impl_->maxSurfaceMutations)
        return "RHI-WEBGL1-SURFACE-LIFECYCLE-OPT-IN-REQUIRED";
    if (impl_->surfaceFailure)
        return impl_->surfaceFailure;
#ifdef __EMSCRIPTEN__
    if (impl_->callerAvailable != available) {
        if (!mutateSurface(*impl_))
            return impl_->surfaceFailure;
        impl_->callerAvailable = available;
    }
    return nullptr;
#else
    return "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#endif
}

const char* WebGlRuntime::resizeSurface(Extent2D extent) noexcept
{
    if (const auto* id = currentDiagnostic())
        return id;
    if (!impl_->maxSurfaceMutations)
        return "RHI-WEBGL1-SURFACE-LIFECYCLE-OPT-IN-REQUIRED";
    if (impl_->surfaceFailure)
        return impl_->surfaceFailure;
    const auto& limits = impl_->profile->limits();
    if (extent.width > INT32_MAX || extent.height > INT32_MAX
        || extent.width > limits.maxViewportWidth || extent.height > limits.maxViewportHeight
        || extent.width > limits.maxRenderbufferSize || extent.height > limits.maxRenderbufferSize)
        return "RHI-WEBGL1-SURFACE-EXTENT-LIMIT";
#ifdef __EMSCRIPTEN__
    if (!mutateSurface(*impl_))
        return impl_->surfaceFailure;
    impl_->logicalSurfaceExtent = extent;
    if (!resizeOriginalCanvas(static_cast<int>(extent.width), static_cast<int>(extent.height)))
        return impl_->surfaceFailure = "RHI-WEBGL1-SURFACE-NATIVE-RESIZE";
    if (extent.width && extent.height) {
        Extent2D actual;
        if (const auto* id = drawingBufferExtent(actual))
            return impl_->surfaceFailure = id;
        if (actual.width != extent.width || actual.height != extent.height)
            return impl_->surfaceFailure = "RHI-WEBGL1-SURFACE-NATIVE-EXTENT-MISMATCH";
    }
    return nullptr;
#else
    return "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#endif
}

const char* WebGlRuntime::currentDiagnostic() const noexcept
{
#ifdef __EMSCRIPTEN__
    if (!emscripten_is_main_runtime_thread() || !emscripten_is_main_browser_thread())
        return "RHI-WEBGL1-CONTEXT-OWNER-THREAD";
    if (emscripten_is_webgl_context_lost(impl_->context))
        contextLost(impl_.get());
    if (impl_->phase == WebGlContextPhase::Failed)
        return impl_->recoveryFailure ? impl_->recoveryFailure
                                      : "RHI-WEBGL1-CONTEXT-RECOVERY-FAILED";
    if (impl_->phase == WebGlContextPhase::Rebuilding)
        return "RHI-WEBGL1-CONTEXT-REBUILDING";
    if (impl_->lost)
        return "RHI-WEBGL1-CONTEXT-LOST";
    if (emscripten_webgl_get_current_context() != impl_->context)
        return "RHI-WEBGL1-CONTEXT-FOREIGN-CURRENT";
    return impl_->profile->diagnostic();
#else
    return "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#endif
}

std::shared_ptr<WebGlRuntime> WebGlRuntime::shareOriginal(core::UniquePtr<WebGlRuntime> runtime)
{
    core::CpuMemoryQuotaScope scope(runtime->impl_->quota);
    return core::shareUnique(std::move(runtime));
}

WebGlContextPhase WebGlRuntime::phase() const noexcept
{
    (void)currentDiagnostic();
    return impl_->phase;
}

const char* WebGlRuntime::enableRecovery(const WebGlContextRecoveryRequest& request) noexcept
{
    if (const auto* id = WebGlContextRecoveryContract::validate(Backend::WebGL, request))
        return id;
    if (const auto* id = currentDiagnostic())
        return id;
    if (impl_->maxRestorations)
        return "RHI-WEBGL1-RECOVERY-ALREADY-CONFIGURED";
    impl_->maxRestorations = request.maxRestorations;
    return nullptr;
}

const char* WebGlRuntime::restore() noexcept
{
#ifndef __EMSCRIPTEN__
    return "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#else
    if (!emscripten_is_main_runtime_thread() || !emscripten_is_main_browser_thread())
        return "RHI-WEBGL1-CONTEXT-OWNER-THREAD";
    if (!impl_->maxRestorations)
        return "RHI-WEBGL1-RECOVERY-OPT-IN-REQUIRED";
    if (impl_->phase != WebGlContextPhase::RestorePending)
        return "RHI-WEBGL1-RECOVERY-NOT-PENDING";
    if (impl_->restorationAttempts >= impl_->maxRestorations)
        return "RHI-WEBGL1-RECOVERY-BUDGET-EXHAUSTED";
    if (emscripten_is_webgl_context_lost(impl_->context))
        return "RHI-WEBGL1-RECOVERY-NATIVE-STILL-LOST";
    if (emscripten_webgl_get_current_context() != impl_->context)
        return "RHI-WEBGL1-CONTEXT-FOREIGN-CURRENT";
    if (!impl_->lifetime.isActive())
        return "RHI-WEBGL1-CONTEXT-GENERATION-EXHAUSTED";
    if (impl_->quota->limitBytes() != impl_->creation.maxCpuMetadataBytes)
        return "RHI-WEBGL1-CONTEXT-CPU-QUOTA";
    impl_->phase = WebGlContextPhase::Rebuilding;
    ++impl_->restorationAttempts;
    try {
        core::CpuMemoryQuotaScope scope(impl_->quota);
        impl_->recoveryFailure = collect(*impl_, impl_->creation);
    } catch (const std::bad_alloc&) {
        impl_->recoveryFailure = "RHI-WEBGL1-RECOVERY-METADATA-ALLOCATION";
    } catch (const std::length_error&) {
        impl_->recoveryFailure = "RHI-WEBGL1-RECOVERY-METADATA-ALLOCATION";
    }
    if (impl_->recoveryFailure) {
        impl_->phase = WebGlContextPhase::Failed;
        return impl_->recoveryFailure;
    }
    impl_->lost = false;
    impl_->phase = WebGlContextPhase::Ready;
    return nullptr;
#endif
}

std::string_view WebGlRuntime::canvasSelector() const noexcept
{
    return impl_->canvas;
}

const char* WebGlRuntime::drawingBufferExtent(Extent2D& extent) const noexcept
{
    if (const auto* id = currentDiagnostic())
        return id;
    if (impl_->maxSurfaceMutations
        && (!impl_->logicalSurfaceExtent.width || !impl_->logicalSurfaceExtent.height))
        return "RHI-WEBGL1-SURFACE-ZERO-EXTENT";
#ifdef __EMSCRIPTEN__
    int width = 0, height = 0;
    if (emscripten_webgl_get_drawing_buffer_size(impl_->context, &width, &height)
            != EMSCRIPTEN_RESULT_SUCCESS
        || width <= 0 || height <= 0)
        return "RHI-WEBGL1-CANVAS-DRAWING-BUFFER-UNAVAILABLE";
    extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    return nullptr;
#else
    (void)extent;
    return "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#endif
}

int WebGlRuntime::beginNativeDestruction() noexcept
{
#ifdef __EMSCRIPTEN__
    if (!emscripten_is_main_runtime_thread() || !emscripten_is_main_browser_thread())
        std::terminate();
    if (impl_->lost || emscripten_is_webgl_context_lost(impl_->context))
        return -1;
    const auto previous = emscripten_webgl_get_current_context();
    if (previous != impl_->context
        && emscripten_webgl_make_context_current(impl_->context) != EMSCRIPTEN_RESULT_SUCCESS)
        std::terminate();
    return previous;
#else
    return -1;
#endif
}

shader::WebGlShaderLimits WebGlRuntime::shaderLimits() const noexcept
{
    shader::WebGlShaderLimits result;
    if (currentDiagnostic())
        return result;
    const auto& l = impl_->probe.limits;
    result.attributes = l.maxVertexAttributes;
    result.varyings = l.maxVaryingVectors;
    result.vertexUniformVectors = l.maxVertexUniformVectors;
    result.fragmentUniformVectors = l.maxFragmentUniformVectors;
    result.vertexTextures = l.maxVertexTextureUnits;
    result.fragmentTextures = l.maxFragmentTextureUnits;
    result.combinedTextures = l.maxCombinedTextureUnits;
    const auto floatHigh = [](const auto& stage) {
        const auto& f = stage[static_cast<std::size_t>(WebGlNumericPrecision::HighFloat)];
        return f.queried && f.precision >= 16 && f.rangeMin >= 62 && f.rangeMax >= 62;
    };
    const auto intHigh = [](const auto& stage) {
        const auto& f = stage[static_cast<std::size_t>(WebGlNumericPrecision::HighInt)];
        return f.queried && f.rangeMin >= 16 && f.rangeMax >= 16;
    };
    result.vertexHighpFloat = floatHigh(impl_->probe.precision.vertex);
    result.fragmentHighpFloat = floatHigh(impl_->probe.precision.fragment);
    result.vertexHighpInt = intHigh(impl_->probe.precision.vertex);
    result.fragmentHighpInt = intHigh(impl_->probe.precision.fragment);
    result.derivatives = impl_->probe.enabledExtensions.derivatives;
    result.textureLod = impl_->shaderTextureLodEnabled;
    result.fragmentDepth = impl_->probe.enabledExtensions.fragmentDepth;
    return result;
}

void WebGlRuntime::endNativeDestruction(int previous) noexcept
{
#ifdef __EMSCRIPTEN__
    if (previous >= 0 && previous != impl_->context
        && emscripten_webgl_make_context_current(previous) != EMSCRIPTEN_RESULT_SUCCESS)
        std::terminate();
#else
    (void)previous;
#endif
}

} // namespace arp::rhi::webgl

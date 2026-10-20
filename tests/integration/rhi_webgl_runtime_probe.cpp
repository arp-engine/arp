// WebGl1Graphics/1 feasibility test. This is not a production RHI executor.
#include "rhi/semantics/webgl_format_facts.h"
#include "rhi/semantics/webgl_runtime_identity.h"
#include "rhi/semantics/webgl_shader_precision.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <array>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <limits>
#include <string>

namespace {

EM_JS(void,
    publishProbeResult,
    (int success,
        const char* message,
        int uploadCopy,
        int textureUpdateCopy,
        int bufferUpdateCopy,
        int issuedDelete,
        int issuedFrames,
        int precisionFactsValidated),
    {
        // clang-format off
            const gl = GL.currentContext && GL.currentContext.GLctx;
            if (!gl) {
                fetch("/result", {
                    method : "POST",
                    headers : {"Content-Type" : "application/json"},
                    body : JSON.stringify({
                        type : "WebGl1RuntimeProbe",
                        contractVersion : 6,
                        success : false,
                        diagnostic : UTF8ToString(message),
                        uploadSourceMutationProbe : false,
                        textureSubImageSourceMutationProbe : false,
                        bufferSubDataSourceMutationProbe : false,
                        issuedObjectDeletionProbe : false,
                        precisionFactsValidated : false,
                        gpuCompletionClaimed : false,
                        productionBackendValidated : false
                    })
                });
                return;
            }
            const names = [
                "MAX_VERTEX_ATTRIBS", "MAX_VERTEX_UNIFORM_VECTORS", "MAX_FRAGMENT_UNIFORM_VECTORS",
                "MAX_VARYING_VECTORS", "MAX_VERTEX_TEXTURE_IMAGE_UNITS", "MAX_TEXTURE_IMAGE_UNITS",
                "MAX_COMBINED_TEXTURE_IMAGE_UNITS", "MAX_TEXTURE_SIZE", "MAX_CUBE_MAP_TEXTURE_SIZE",
                "MAX_RENDERBUFFER_SIZE", "MAX_VIEWPORT_DIMS"
            ];
            const limits = {};
            for (const name of names) {
                const value = gl.getParameter(gl[name]);
                limits[name] = ArrayBuffer.isView(value) ? Array.from(value) : value;
            }
            const precisionFormats =
                [ "LOW_FLOAT", "MEDIUM_FLOAT", "HIGH_FLOAT", "LOW_INT", "MEDIUM_INT", "HIGH_INT" ];
            const precision = {vertex : {}, fragment : {}};
            for (const [stage, shader] of [[ "vertex", gl.VERTEX_SHADER ],
                                           [ "fragment", gl.FRAGMENT_SHADER ]]) {
                for (const name of precisionFormats) {
                    const format = gl.getShaderPrecisionFormat(shader, gl[name]);
                    precision[stage][name] = format ? {
                        queried : true,
                        rangeMin : format.rangeMin,
                        rangeMax : format.rangeMax,
                        precision : format.precision
                    }
                                                    : {queried : false};
                }
            }
            fetch("/result", {
                method : "POST",
                headers : {"Content-Type" : "application/json"},
                body : JSON.stringify({
                    type : "WebGl1RuntimeProbe",
                    contractVersion : 6,
                    success : !!success,
                    diagnostic : UTF8ToString(message),
                    version : gl.getParameter(gl.VERSION),
                    shadingLanguage : gl.getParameter(gl.SHADING_LANGUAGE_VERSION),
                    renderer : gl.getParameter(gl.RENDERER),
                    limits,
                    extensions : gl.getSupportedExtensions(),
                    contextAttributes : gl.getContextAttributes(),
                    precision,
                    precisionFactsValidated : !!precisionFactsValidated,
                    formatFacts : Module.webGlFormatFacts || null,
                    identityFacts : Module.webGlIdentityFacts || null,
                    retirementFacts : Module.webGlRetirementFacts || null,
                    gpuCompletionClaimed : false,
                    uploadSourceMutationProbe : !!uploadCopy,
                    textureSubImageSourceMutationProbe : !!textureUpdateCopy,
                    bufferSubDataSourceMutationProbe : !!bufferUpdateCopy,
                    issuedObjectDeletionProbe : !!issuedDelete,
                    issuedFrames,
                    productionBackendValidated : false
                })
            });
        // clang-format on
    });

EM_JS(void, recordRetirementFact, (const char* name, int passed), {
    // clang-format off
        if (!Module.webGlRetirementFacts)
            Module.webGlRetirementFacts = {contractVersion : 1};
        Module.webGlRetirementFacts[UTF8ToString(name)] = !!passed;
    // clang-format on
});

EM_JS(int, nativeIdentityText, (int field, char* destination, int capacity), {
    // clang-format off
        const names = [ "contextVersion", "shaderVersion", "vendor", "renderer", "browserUserAgent" ];
        const glNames = [ "VERSION", "SHADING_LANGUAGE_VERSION", "VENDOR", "RENDERER" ];
        const gl = GL.currentContext && GL.currentContext.GLctx;
        if (!gl || gl.isContextLost() || field < 0 || field >= names.length)
            return -1;
        if (!Module.webGlIdentityFacts)
            Module.webGlIdentityFacts =
                {contractVersion : 1, validated : false, sourceAuthenticated : false, fields : {}};
        const fields = Module.webGlIdentityFacts.fields;
        if (!destination) {
            const value =
                field < glNames.length ? gl.getParameter(gl[glNames[field]]) : navigator.userAgent;
            if (typeof value !== "string")
                return -1;
            fields[names[field]] = {queried : true, value};
            return lengthBytesUTF8(value);
        }
        const entry = fields[names[field]];
        if (!entry || capacity <= lengthBytesUTF8(entry.value))
            return -1;
        stringToUTF8(entry.value, destination, capacity);
        return lengthBytesUTF8(entry.value);
    // clang-format on
});

EM_JS(void, recordIdentityValidation, (int validated), {
    // clang-format off
        if (Module.webGlIdentityFacts)
            Module.webGlIdentityFacts.validated = !!validated;
    // clang-format on
});

bool validateNativeIdentityFacts()
{
    using namespace arp::rhi::semantics;
    WebGlRuntimeIdentityView identity;
    std::array<std::string, webGlIdentityFieldCount> storage;
    for (std::size_t index = 0u; index < storage.size(); ++index) {
        const auto length = nativeIdentityText(static_cast<int>(index), nullptr, 0);
        if (length < 0 || length == std::numeric_limits<int>::max())
            continue;
        auto& text = storage[index];
        text.resize(static_cast<std::size_t>(length) + 1u);
        const auto copied = nativeIdentityText(static_cast<int>(index), text.data(), length + 1);
        const bool queried = copied == length && glGetError() == GL_NO_ERROR;
        text.resize(static_cast<std::size_t>(length));
        identity[index] = {queried, {text.data(), text.size()}};
    }
    const bool valid = !emscripten_is_webgl_context_lost(emscripten_webgl_get_current_context())
        && WebGlRuntimeIdentityContract::validate(identity) == nullptr;
    recordIdentityValidation(valid);
    return valid;
}

EM_JS(void,
    recordFormatFacts,
    (int validated,
        int textureOutcome,
        int uploadAccepted,
        int colorOutcome,
        int colorComplete,
        int packedOutcome,
        int depthBits,
        int stencilBits,
        int packedComplete),
    {
        // clang-format off
            Module.webGlFormatFacts = {
                contractVersion : 1,
                validated : !!validated,
                rgba8TextureOutcome : textureOutcome,
                rgba8ClientUploadAccepted : !!uploadAccepted,
                rgba8FramebufferOutcome : colorOutcome,
                rgba8FramebufferComplete : !!colorComplete,
                packedDepthStencilOutcome : packedOutcome,
                packedDepthBits : depthBits,
                packedStencilBits : stencilBits,
                packedFramebufferComplete : !!packedComplete
            };
        // clang-format on
    });

// WebGL1 adds this combined attachment beyond GLES2's native header tokens.
// Read its named constant from the actual context; never invent an enum value.
EM_JS(GLenum, packedDepthStencilAttachment, (), {
    // clang-format off
        return GL.currentContext.GLctx.DEPTH_STENCIL_ATTACHMENT;
    // clang-format on
});

bool validateNativeFormatFacts()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;
    WebGlFormatFacts facts;
    facts.contractVersion = 1u;
    const auto outcome = [](GLenum error) {
        if (emscripten_is_webgl_context_lost(emscripten_webgl_get_current_context()))
            return WebGlFormatQueryOutcome::ContextLost;
        switch (error) {
        case GL_NO_ERROR:
            return WebGlFormatQueryOutcome::Observed;
        case GL_OUT_OF_MEMORY:
            return WebGlFormatQueryOutcome::OutOfMemory;
        default:
            return WebGlFormatQueryOutcome::NativeFailure;
        }
    };
    GLuint texture = 0, framebuffer = 0, packed = 0;
    facts.rgba8Texture = outcome(glGetError());
    if (facts.rgba8Texture == WebGlFormatQueryOutcome::Observed) {
        glGenTextures(1, &texture);
        facts.rgba8Texture = outcome(glGetError());
        if (!texture && facts.rgba8Texture == WebGlFormatQueryOutcome::Observed)
            facts.rgba8Texture = WebGlFormatQueryOutcome::NativeFailure;
    }
    if (facts.rgba8Texture == WebGlFormatQueryOutcome::Observed) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const std::array<GLubyte, 4u> pixel{0, 0, 0, 255};
        // Explicit cold test workload: exactly one RGBA8 texel and one
        // single-sample packed renderbuffer, not a resource-size guarantee.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        facts.rgba8Texture = outcome(glGetError());
        facts.rgba8ClientUploadAccepted = facts.rgba8Texture == WebGlFormatQueryOutcome::Observed;
    }
    if (facts.rgba8ClientUploadAccepted) {
        glGenFramebuffers(1, &framebuffer);
        facts.rgba8Framebuffer = outcome(glGetError());
        if (!framebuffer && facts.rgba8Framebuffer == WebGlFormatQueryOutcome::Observed)
            facts.rgba8Framebuffer = WebGlFormatQueryOutcome::NativeFailure;
    }
    if (facts.rgba8Framebuffer == WebGlFormatQueryOutcome::Observed) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        facts.rgba8FramebufferComplete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        facts.rgba8Framebuffer = outcome(glGetError());
    }
    if (facts.rgba8Framebuffer == WebGlFormatQueryOutcome::Observed) {
        glGenRenderbuffers(1, &packed);
        facts.packedDepthStencil = outcome(glGetError());
        if (!packed && facts.packedDepthStencil == WebGlFormatQueryOutcome::Observed)
            facts.packedDepthStencil = WebGlFormatQueryOutcome::NativeFailure;
    }
    if (facts.packedDepthStencil == WebGlFormatQueryOutcome::Observed) {
        glBindRenderbuffer(GL_RENDERBUFFER, packed);
        // GLES2's named token maps to WebGL1 DEPTH_STENCIL in Emscripten's
        // glRenderbufferStorage bridge; it does not grant depth-texture support.
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_STENCIL_OES, 1, 1);
        GLint depthBits = 0, stencilBits = 0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &depthBits);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &stencilBits);
        facts.packedDepthBits = depthBits;
        facts.packedStencilBits = stencilBits;
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, packedDepthStencilAttachment(), GL_RENDERBUFFER, packed);
        facts.packedFramebufferComplete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        facts.packedDepthStencil = outcome(glGetError());
    }
    bool attachments = false;
    bool detached = false;
    if (framebuffer && packed && texture) {
        GLint colorType = 0, colorName = 0, packedType = 0, packedName = 0;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
            &colorType);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
            &colorName);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
            packedDepthStencilAttachment(),
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
            &packedType);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
            packedDepthStencilAttachment(),
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
            &packedName);
        attachments = colorType == GL_TEXTURE && static_cast<GLuint>(colorName) == texture
            && packedType == GL_RENDERBUFFER && static_cast<GLuint>(packedName) == packed
            && glGetError() == GL_NO_ERROR;
    }
    recordRetirementFact("framebufferAttachmentsMatched", attachments);
    if (framebuffer) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, packedDepthStencilAttachment(), GL_RENDERBUFFER, 0);
        GLint colorType = -1, packedType = -1;
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
            GL_COLOR_ATTACHMENT0,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
            &colorType);
        glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
            packedDepthStencilAttachment(),
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,
            &packedType);
        detached = colorType == GL_NONE && packedType == GL_NONE && glGetError() == GL_NO_ERROR;
    }
    recordRetirementFact("framebufferAttachmentsDetached", detached);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    GLint framebufferBinding = -1, renderbufferBinding = -1, textureBinding = -1;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferBinding);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbufferBinding);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding);
    const bool bindingsCleared = framebufferBinding == 0 && renderbufferBinding == 0
        && textureBinding == 0 && glGetError() == GL_NO_ERROR;
    recordRetirementFact("framebufferBindingsCleared", bindingsCleared);
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    const bool framebufferRetired = framebuffer && !glIsFramebuffer(framebuffer) && packed
        && glIsRenderbuffer(packed) && texture && glIsTexture(texture)
        && glGetError() == GL_NO_ERROR;
    recordRetirementFact("framebufferRetiredChildrenLive", framebufferRetired);
    if (packed)
        glDeleteRenderbuffers(1, &packed);
    const bool renderbufferRetired = packed && !glIsRenderbuffer(packed) && texture
        && glIsTexture(texture) && glGetError() == GL_NO_ERROR;
    recordRetirementFact("renderbufferRetiredTextureLive", renderbufferRetired);
    if (texture)
        glDeleteTextures(1, &texture);
    const bool textureRetired = texture && !glIsTexture(texture) && glGetError() == GL_NO_ERROR;
    recordRetirementFact("attachmentTextureRetired", textureRetired);
    const bool valid = attachments && detached && bindingsCleared && framebufferRetired
        && renderbufferRetired && textureRetired
        && WebGlFormatContract::assess(facts,
               {1u, TextureFormat::Rgba8Unorm, WebGlFormatRole::Texture2DColorAttachment, 1u})
            == nullptr;
    recordFormatFacts(valid,
        static_cast<int>(facts.rgba8Texture),
        facts.rgba8ClientUploadAccepted,
        static_cast<int>(facts.rgba8Framebuffer),
        facts.rgba8FramebufferComplete,
        static_cast<int>(facts.packedDepthStencil),
        facts.packedDepthBits,
        facts.packedStencilBits,
        facts.packedFramebufferComplete);
    return valid;
}

std::string readSource(const char* path)
{
    auto* file = std::fopen(path, "rb");
    if (!file)
        return {};
    std::string result;
    std::array<char, 1024> bytes;
    while (auto size = std::fread(bytes.data(), 1, bytes.size(), file))
        result.append(bytes.data(), size);
    std::fclose(file);
    return result;
}

GLuint compile(GLenum stage, const char* path)
{
    auto source = readSource(path);
    const auto* text = source.c_str();
    auto shader = glCreateShader(stage);
    glShaderSource(shader, 1, &text, nullptr);
    for (auto& value : source)
        value = '\0';
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    const bool copied = ok && glGetError() == GL_NO_ERROR;
    recordRetirementFact(
        stage == GL_VERTEX_SHADER ? "vertexSourceCopied" : "fragmentSourceCopied", copied);
    if (!copied) {
        std::array<char, 4096> log{};
        glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
        std::fprintf(stderr, "%s: %s\n", path, log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool pixel(GLint x, GLint y, std::array<GLubyte, 4> expected)
{
    std::array<GLubyte, 4> actual{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
    if (actual == expected)
        return true;
    std::fprintf(
        stderr, "pixel(%d,%d) = %u,%u,%u,%u\n", x, y, actual[0], actual[1], actual[2], actual[3]);
    return false;
}

struct ProbeFrameState {
    GLuint program = 0;
    GLuint texture = 0;
    GLuint buffer = 0;
    GLint tintLocation = -1;
    int frameBudget = 1;
    int issuedFrames = 0;
    bool success = false;
    bool uploadCopy = false;
    bool textureUpdateCopy = false;
    bool bufferUpdateCopy = false;
    bool issuedDelete = false;
    bool captureGate = false;
    bool captureRequested = false;
    bool precisionFactsValidated = false;
};

ProbeFrameState frames;

void publishProbe(int success, const char* message)
{
    publishProbeResult(success,
        message,
        frames.uploadCopy,
        frames.textureUpdateCopy,
        frames.bufferUpdateCopy,
        frames.issuedDelete,
        frames.issuedFrames,
        frames.precisionFactsValidated);
}

// Cold fixture only: query the actual GLES2 bridge and consume the same
// numeric contract used by the host profile tests. No capability promotion.
bool validateNativePrecisionFacts()
{
    using namespace arp::rhi::semantics;
    WebGlShaderPrecisionFacts facts;
    WebGlPrecisionRequirements requirements;
    requirements.contractVersion = 1u;
    constexpr std::array<GLenum, kWebGlNumericPrecisionCount> categories{
        GL_LOW_FLOAT, GL_MEDIUM_FLOAT, GL_HIGH_FLOAT, GL_LOW_INT, GL_MEDIUM_INT, GL_HIGH_INT};
    constexpr std::array<GLenum, 2u> stages{GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
    const std::array factStages{&facts.vertex, &facts.fragment};
    const std::array demandStages{&requirements.vertex, &requirements.fragment};
    if (glGetError() != GL_NO_ERROR)
        return false;
    for (std::size_t stage = 0; stage != stages.size(); ++stage) {
        for (std::size_t i = 0; i != categories.size(); ++i) {
            GLint range[2]{};
            GLint precision = 0;
            glGetShaderPrecisionFormat(stages[stage], categories[i], range, &precision);
            const bool queried = glGetError() == GL_NO_ERROR;
            (*factStages[stage])[i] = {queried, range[0], range[1], precision};
            const bool supported = i < static_cast<std::size_t>(WebGlNumericPrecision::LowInt)
                ? precision > 0
                : range[0] > 0 || range[1] > 0;
            if (supported)
                (*demandStages[stage])[i] = {true, range[0], range[1], precision};
        }
    }
    return WebGlShaderPrecisionContract::assess(facts, requirements) == nullptr;
}

void drawProbeScene()
{
    glUseProgram(frames.program);
    glViewport(0, 0, 64, 64);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClearDepthf(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 32, 64);
    glUniform4f(frames.tintLocation, 1, 0, 0, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glScissor(32, 0, 32, 64);
    glUniform4f(frames.tintLocation, 0, 1, 0, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// Capture-only repeated workload; final-frame readback is a cold oracle.
// No frame count or successful oracle grants GPU completion authority.
void drawProbeFrame()
{
    // Cold capture coordination only. The host acknowledges after arming
    // RenderDoc; this HTTP marker is neither queue nor completion evidence.
    if (frames.captureGate && frames.issuedFrames + 1 == frames.frameBudget) {
        if (!frames.captureRequested) {
            frames.captureRequested = true;
            EM_ASM({
                // clang-format off
                    fetch("/probe-ready", {method : "POST", body : "final-frame"})
                        .then(response => response.text())
                        .then(value => { Module.webgl1CaptureArmed = value === "capture-armed"; });
                // clang-format on
            });
        }
        if (!EM_ASM_INT({
                // clang-format off
                return Module.webgl1CaptureArmed === true;
                // clang-format on
            }))
            return;
    }
    drawProbeScene();
    ++frames.issuedFrames;
    if (frames.issuedFrames == 1 && frames.frameBudget > 1 && !frames.captureGate) {
        EM_ASM({
            // clang-format off
                fetch("/probe-ready", {method : "POST", body : "issued"});
            // clang-format on
        });
    }
    if (frames.issuedFrames < frames.frameBudget)
        return;
    frames.uploadCopy = pixel(28, 32, {128, 0, 0, 255}) && pixel(40, 32, {0, 255, 0, 255})
        && pixel(18, 32, {0, 0, 0, 255}) && glGetError() == GL_NO_ERROR;

    // Change existing storage, then destroy the client data before issuing
    // draws. A no-op update or a retained pointer yields different pixels.
    GLubyte updatedTexel[] = {64, 128, 255, 255};
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, updatedTexel);
    for (auto& value : updatedTexel)
        value = 0;
    drawProbeScene();
    frames.textureUpdateCopy = pixel(28, 32, {64, 0, 0, 255}) && pixel(40, 32, {0, 128, 0, 255})
        && pixel(18, 32, {0, 0, 0, 255}) && glGetError() == GL_NO_ERROR;

    // Replace only vertex 2, keeping vertex 0/1 and the buffer allocation.
    // This moves the triangle below the old center probes. Positive bottom
    // probes distinguish applied geometry from a zeroed/empty upload.
    GLfloat updatedVertex[] = {-1, -5, 0, 0, 1};
    glBufferSubData(GL_ARRAY_BUFFER, 10 * sizeof(GLfloat), sizeof(updatedVertex), updatedVertex);
    for (auto& value : updatedVertex)
        value = 0;
    drawProbeScene();

    // Delete unbound object names after issuing draws and BEFORE readback.
    // Only issued-work preservation is tested, never physical VRAM release.
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDeleteBuffers(1, &frames.buffer);
    glDeleteTextures(1, &frames.texture);
    glDeleteProgram(frames.program);
    const bool deletedNames = glIsBuffer(frames.buffer) == GL_FALSE
        && glIsTexture(frames.texture) == GL_FALSE && glIsProgram(frames.program) == GL_FALSE;
    frames.bufferUpdateCopy = pixel(28, 32, {0, 0, 0, 255}) && pixel(40, 32, {0, 0, 0, 255})
        && pixel(28, 10, {64, 0, 0, 255}) && pixel(40, 10, {0, 128, 0, 255})
        && glGetError() == GL_NO_ERROR;
    frames.issuedDelete = deletedNames && frames.bufferUpdateCopy;
    frames.success = frames.uploadCopy && frames.textureUpdateCopy && frames.bufferUpdateCopy
        && frames.issuedDelete;
    publishProbe(
        frames.success, frames.success ? "RHI-WEBGL1-PROBE-PASSED" : "RHI-WEBGL1-PROBE-NUMERICAL");
    if (frames.frameBudget > 1)
        emscripten_cancel_main_loop();
}

} // namespace

int main()
{
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = true;
    attrs.stencil = false;
    attrs.antialias = false;
    attrs.premultipliedAlpha = false;
    attrs.preserveDrawingBuffer = true; // cold test capture only
    attrs.enableExtensionsByDefault = false;
    auto context = emscripten_webgl_create_context("#canvas", &attrs);
    if (context <= 0
        || emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS) {
        std::fprintf(stderr, "RHI-WEBGL1-PROBE-CONTEXT\n");
        publishProbe(0, "RHI-WEBGL1-PROBE-CONTEXT");
        return 1;
    }
    if (!validateNativeIdentityFacts()) {
        publishProbe(0, "RHI-WEBGL1-PROBE-IDENTITY-FACTS");
        return 1;
    }
    if (!validateNativeFormatFacts()) {
        publishProbe(0, "RHI-WEBGL1-PROBE-FORMAT-FACTS");
        return 1;
    }
    frames.precisionFactsValidated = validateNativePrecisionFacts();
    if (!frames.precisionFactsValidated) {
        publishProbe(0, "RHI-WEBGL1-PROBE-PRECISION-FACTS");
        return 1;
    }
    auto vs = compile(GL_VERTEX_SHADER, "/probe.vert");
    auto fs = compile(GL_FRAGMENT_SHADER, "/probe.frag");
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        publishProbe(0, "RHI-WEBGL1-PROBE-SHADER-COMPILE");
        return 1;
    }
    auto program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "input_position");
    glBindAttribLocation(program, 1, "input_uv");
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::array<char, 4096> log{};
        glGetProgramInfoLog(program, log.size(), nullptr, log.data());
        std::fprintf(stderr, "link: %s\n", log.data());
        publishProbe(0, "RHI-WEBGL1-PROBE-LINK");
        return 1;
    }
    std::array<GLuint, 2u> attached{};
    GLsizei attachedCount = 0;
    glGetAttachedShaders(
        program, static_cast<GLsizei>(attached.size()), &attachedCount, attached.data());
    const bool attachmentNames = attachedCount == 2
        && ((attached[0] == vs && attached[1] == fs) || (attached[0] == fs && attached[1] == vs))
        && glGetError() == GL_NO_ERROR;
    recordRetirementFact("shaderAttachmentsMatched", attachmentNames);
    // Emscripten clears GL.shaders[id] on delete. Detach while both bridge
    // names still address their original WebGLShader objects.
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    GLint remaining = -1;
    glGetProgramiv(program, GL_ATTACHED_SHADERS, &remaining);
    const bool shadersDetached = remaining == 0 && glGetError() == GL_NO_ERROR;
    recordRetirementFact("shadersDetached", shadersDetached);
    glDeleteShader(vs);
    glDeleteShader(fs);
    const bool shaderNamesRetired =
        !glIsShader(vs) && !glIsShader(fs) && glGetError() == GL_NO_ERROR;
    recordRetirementFact("shaderNamesRetired", shaderNamesRetired);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    const bool programRetained = glIsProgram(program) && linked && glGetError() == GL_NO_ERROR;
    recordRetirementFact("linkedProgramRetained", programRetained);
    if (!attachmentNames || !shadersDetached || !shaderNamesRetired || !programRetained) {
        glDeleteProgram(program);
        publishProbe(0, "RHI-WEBGL1-PROBE-SHADER-RETIREMENT");
        return 1;
    }
    glUseProgram(program);
    const auto location = [&](const char* name) {
        return glGetUniformLocation(program, name);
    };
    const auto matrixLoc = location("frameConstants.viewProjection");
    const auto tintLoc = location("frameConstants.tint");
    const auto valueLoc = location("frameConstants.values.data[0]");
    const auto offsetLoc = location("drawConstants.offset");
    const auto samplerLoc = location("SPIRV_Cross_CombinedcolorTexturecolorSampler");
    if (matrixLoc < 0 || tintLoc < 0 || valueLoc < 0 || offsetLoc < 0 || samplerLoc < 0) {
        publishProbe(0, "RHI-WEBGL1-PROBE-UNIFORM-ABI");
        return 1;
    }
    // Row-major CPU bytes; generated ES100 uses row-vector multiplication.
    GLfloat matrix[] = {.5f, 0, 0, .2f, 0, .5f, 0, .1f, 0, 0, 1, 0, 0, 0, 0, 1};
    GLfloat values[] = {1, 1, 1, 1, 0, 0, 0, 0};
    glUniformMatrix4fv(matrixLoc, 1, GL_FALSE, matrix);
    glUniform4fv(valueLoc, 2, values);
    // Destroy client values after the bridge returns, before any draw. The
    // cold pixel oracle must still observe the original uploads/uniforms.
    for (auto& value : matrix)
        value = 0;
    for (auto& value : values)
        value = 0;
    glUniform4f(offsetLoc, 0, 0, 0, 0);
    glUniform1i(samplerLoc, 0);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLubyte texel[] = {128, 255, 64, 255};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
    for (auto& value : texel)
        value = 0;
    GLfloat vertices[] = {-1, -1, 0, 0, 0, 3, -1, 0, 1, 0, -1, 3, 0, 0, 1};
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    for (auto& value : vertices)
        value = 0;
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
    glVertexAttribPointer(1,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(GLfloat),
        reinterpret_cast<void*>(3 * sizeof(GLfloat)));
    frames.program = program;
    frames.texture = texture;
    frames.buffer = buffer;
    frames.tintLocation = tintLoc;
    frames.captureGate = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).get("captureGate") === "1";
        // clang-format on
    });
    frames.frameBudget = EM_ASM_INT({
        // clang-format off
            const value = new URLSearchParams(location.search).get("captureFrames");
            if (value === null)
                return 1;
            const parsed = Number(value);
            return Number.isInteger(parsed) && parsed > 0 && parsed <= 2147483647 ? parsed : 0;
        // clang-format on
    });
    if (!frames.frameBudget || (frames.captureGate && frames.frameBudget < 2)) {
        publishProbe(0, "RHI-WEBGL1-PROBE-CAPTURE-BUDGET");
        glDeleteBuffers(1, &frames.buffer);
        glDeleteTextures(1, &frames.texture);
        glDeleteProgram(frames.program);
        return 1;
    }
    if (frames.frameBudget > 1) {
        emscripten_set_main_loop(drawProbeFrame, 0, false);
        return 0;
    }
    drawProbeFrame();
    return frames.success ? 0 : 1;
}

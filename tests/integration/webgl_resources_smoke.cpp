#include "rhi/backends/webgl/webgl_resources_internal.h"

#include <GLES2/gl2.h>
#include <array>
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>

using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi::semantics;

namespace {
EM_JS(void, report, (int success, const char* diagnostic, int checks), {
    // clang-format off
    fetch('/result', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'WebGlOwnedResourcesSmoke',contractVersion:1,success:!!success,diagnostic:UTF8ToString(diagnostic),checks,
        webGpuUnavailable:navigator.gpu===undefined,subscriptionsRetired:Module.webGlOwnedSubscriptions.size===0,
        productionBackendValidated:false,visualAcceptanceClaimed:false,gpuCompletionClaimed:false})});
    // clang-format on
});

class ResourcesSmoke final {
public:
    bool check(bool success, const char* diagnostic)
    {
        if (!success) {
            failure = diagnostic;
            return false;
        }
        ++checks;
        return true;
    }

    bool run();
    const char* failure = "";
    int checks = 0;
};

#define REQUIRE(expression, id)                                                                    \
    do {                                                                                           \
        if (!check(bool(expression), id))                                                          \
            return false;                                                                          \
    } while (false)

bool ResourcesSmoke::run()
{
    WebGlContextOwnershipRequest context;
    context.contractVersion = context.context.contractVersion = 1;
    context.canvasSelector = "#canvas";
    context.colorSpace = WebGlCanvasColorSpace::SRgb;
    context.maxCpuMetadataBytes = 256 * 1024;
    context.maxIdentityFieldBytes = 4096;
    auto contextQuota = core::makeShared<core::CpuMemoryQuota>(context.maxCpuMetadataBytes);
    const char* diagnostic = nullptr;
    auto runtime = webgl::WebGlRuntime::create(context, contextQuota, diagnostic);
    REQUIRE(runtime, diagnostic ? diagnostic : "RESOURCE-CONTEXT-MISSING");
    WebGlResourceOwnershipRequest request;
    request.contractVersion = request.budget.contractVersion = 1;
    auto& capacity = request.budget.capacity;
    // Explicit fixture workload budgets. No runtime default is inferred.
    capacity.cpuMetadataBytes = 1024 * 1024;
    capacity.cpuUploadBytes = 128 * 1024;
    capacity.logicalBufferBytes = 64;
    capacity.logicalTextureBytes = 64;
    capacity.objectCount = 8;
    auto metadata = core::makeShared<core::CpuMemoryQuota>(capacity.cpuMetadataBytes);
    auto uploadQuota = core::makeShared<core::CpuMemoryQuota>(capacity.cpuUploadBytes);
    auto resources = webgl::WebGlResources::create(
        std::move(runtime), request, metadata, uploadQuota, diagnostic);
    REQUIRE(resources && !diagnostic, diagnostic ? diagnostic : "RESOURCE-STORE-MISSING");
    const auto ownContext = emscripten_webgl_get_current_context();
    BufferDesc bd;
    bd.label.assign(128, 'b');
    bd.sizeBytes = 16;
    bd.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
    auto vertex = resources->createBuffer(bd);
    REQUIRE(
        vertex && resources->counters().logicalBufferBytes == 16, "RESOURCE-NATIVE-VERTEX-CREATE");
    auto* v = dynamic_cast<webgl::WebGlBuffer*>(vertex.get());
    REQUIRE(vertex->desc().label.get_allocator().resource() == &core::defaultCpuMemoryResource(),
        "RESOURCE-BUFFER-CORE-ALLOCATOR");
    REQUIRE(v && v->payload->native && glIsBuffer(v->payload->native),
        "RESOURCE-NATIVE-VERTEX-PAYLOAD");
    REQUIRE(!v->payload->content->initialized(0, 16), "RESOURCE-ZERO-IS-NOT-INITIALIZED");
    const std::array<std::uint8_t, 16> source{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    REQUIRE(resources->writeBuffer(*vertex, source.data(), 8, 0).success,
        "RESOURCE-PARTIAL-BUFFER-WRITE");
    REQUIRE(v->payload->content->initialized(0, 8) && !v->payload->content->initialized(0, 16),
        "RESOURCE-BUFFER-EXACT-COVERAGE");
    REQUIRE(
        !resources->writeBuffer(*vertex, source.data(), 16, 1).success && v->payload->revision == 1,
        "RESOURCE-BUFFER-RANGE-PREFLIGHT");
    REQUIRE(resources->writeBuffer(*vertex, source.data() + 8, 8, 8).success
            && v->payload->content->initialized(0, 16),
        "RESOURCE-BUFFER-COMPLETE-WRITE");
    bd.usage = ResourceUsage::IndexBuffer | ResourceUsage::CopyDst;
    auto indices = resources->createBuffer(bd);
    REQUIRE(indices && indices->id() != vertex->id(), "RESOURCE-INDEX-IDENTITY");
    bd.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
    auto uniform = resources->createBuffer(bd);
    REQUIRE(uniform && dynamic_cast<webgl::WebGlBuffer*>(uniform.get())->payload->native == 0,
        "RESOURCE-UNIFORM-CPU-SOURCE-NOT-NATIVE-UBO");
    REQUIRE(resources->writeBuffer(*uniform, source.data(), 16, 0).success,
        "RESOURCE-UNIFORM-EXPLICIT-WRITE");
    auto overflow = bd;
    overflow.sizeBytes = 32;
    REQUIRE(!resources->createBuffer(overflow) && resources->counters().logicalBufferBytes == 48,
        "RESOURCE-LOGICAL-BUDGET-ROLLBACK");
    vertex->sealHostWrites();
    REQUIRE(!resources->writeBuffer(*vertex, source.data(), 16, 0).success, "RESOURCE-HOST-SEAL");

    TextureDesc td;
    td.label.assign(128, 't');
    td.extent = {2, 2, 1};
    td.format = TextureFormat::Rgba8Unorm;
    td.usage = TextureUsage::Sampled | TextureUsage::RenderAttachment | TextureUsage::CopyDst;
    auto color = resources->createTexture(td);
    REQUIRE(
        color && resources->counters().logicalTextureBytes == 16, "RESOURCE-RGBA8-NATIVE-CREATE");
    auto* colorObject = dynamic_cast<webgl::WebGlTexture*>(color.get());
    REQUIRE(color->desc().label.get_allocator().resource() == &core::defaultCpuMemoryResource(),
        "RESOURCE-TEXTURE-CORE-ALLOCATOR");
    REQUIRE(
        colorObject && glIsTexture(colorObject->payload->native), "RESOURCE-RGBA8-NATIVE-PAYLOAD");
    const std::array<std::uint8_t, 24> pixels{255,
        0,
        0,
        255,
        0,
        255,
        0,
        255,
        77,
        88,
        99,
        11,
        0,
        0,
        255,
        255,
        255,
        255,
        255,
        255,
        22,
        33,
        44,
        55};
    WebGlColdTextureUpload up;
    up.contractVersion = 1;
    up.extent = {2, 1, 1};
    up.bytesPerRow = 12;
    up.bytes = {pixels.data(), 12};
    REQUIRE(resources->uploadTexture(*color, up).success
            && !colorObject->payload->content->initialized(0, 16),
        "RESOURCE-TEXTURE-PARTIAL-COVERAGE");
    up.origin.height = 1;
    up.bytes = {pixels.data() + 12, 12};
    REQUIRE(resources->uploadTexture(*color, up).success
            && colorObject->payload->content->initialized(0, 16),
        "RESOURCE-TEXTURE-COMPLETE-COVERAGE");
    up.bytes = {pixels.data(), 11};
    REQUIRE(!resources->uploadTexture(*color, up).success && colorObject->payload->revision == 2,
        "RESOURCE-TEXTURE-ROW-BOUNDS");
    // Native readback is a test-only oracle; the production resource component
    // does not publish readback or completion capability.
    GLuint oracle = 0;
    glGenFramebuffers(1, &oracle);
    glBindFramebuffer(GL_FRAMEBUFFER, oracle);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorObject->payload->native, 0);
    std::array<std::uint8_t, 16> actual{};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &oracle);
    const std::array<std::uint8_t, 16> expected{
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    REQUIRE(glGetError() == GL_NO_ERROR && actual == expected, "RESOURCE-ACTUAL-GPU-ROW-LAYOUT");
    auto zd = td;
    zd.format = TextureFormat::Depth24Stencil8;
    zd.usage = TextureUsage::RenderAttachment;
    auto depth = resources->createTexture(zd);
    REQUIRE(
        depth, resources->diagnostic() ? resources->diagnostic() : "RESOURCE-PACKED-DEPTH-CREATE");
    TextureViewDesc vd;
    vd.label.assign(128, 'v');
    vd.format = td.format;
    vd.usage = TextureUsage::Sampled;
    auto view = resources->createTextureView(*color, vd);
    REQUIRE(view && view->textureId() == color->id() && view->id() != color->id(),
        "RESOURCE-VIEW-PARENT-IDENTITY");
    REQUIRE(view->desc().label.get_allocator().resource() == &core::defaultCpuMemoryResource(),
        "RESOURCE-VIEW-CORE-ALLOCATOR");
    auto badView = vd;
    badView.format = TextureFormat::Rgba8UnormSrgb;
    REQUIRE(!resources->createTextureView(*color, badView), "RESOURCE-REINTERPRET-REJECTED");
    auto fbo = resources->createFramebuffer(*color, depth.get());
    REQUIRE(fbo && !fbo->currentDiagnostic(),
        resources->diagnostic() ? resources->diagnostic() : "RESOURCE-COLD-FBO-COMPLETE");
    const auto textureId = color->id();
    const auto textureNative = colorObject->payload->native;
    std::weak_ptr<webgl::WebGlTexturePayload> retained = colorObject->payload;
    color.reset();
    depth.reset();
    REQUIRE(resources->counters().logicalTextureBytes == 32 && !retained.expired()
            && glIsTexture(textureNative),
        "RESOURCE-ATTACHMENT-CHARGES-RETAINED");
    view.reset();
    REQUIRE(!retained.expired() && !fbo->currentDiagnostic(), "RESOURCE-FBO-RETAINS-PARENTS");
    fbo.reset();
    REQUIRE(retained.expired() && resources->counters().logicalTextureBytes == 0
            && !glIsTexture(textureNative),
        "RESOURCE-LAST-OWNER-RETIRES-NATIVE");
    retained.reset();
    auto newColor = resources->createTexture(td);
    REQUIRE(newColor && newColor->id() != textureId, "RESOURCE-ID-NO-ABA");

    SamplerDesc sd;
    sd.label.assign(128, 's');
    sd.mipFilter = SamplerMipFilter::None;
    core::pmr::Vector<core::UniquePtr<GpuSampler>> samplers{&core::defaultCpuMemoryResource()};
    samplers.reserve(8);
    for (unsigned i = 0; i < 8; ++i) {
        auto sampler = resources->createSampler(sd);
        if (!sampler)
            break;
        REQUIRE(
            sampler->desc().label.get_allocator().resource() == &core::defaultCpuMemoryResource(),
            "RESOURCE-SAMPLER-CORE-ALLOCATOR");
        samplers.push_back(std::move(sampler));
    }
    REQUIRE(resources->counters().objectCount == 8 && resources->diagnostic()
            && std::string_view(resources->diagnostic()) == "RHI-WEBGL1-MEMORY-CAPACITY",
        "RESOURCE-OBJECT-COUNT-BOUND");
    samplers.clear();
    // A distinct real browser context must not receive deletes for matching
    // numeric names. Destruction restores its previous current context.
    EmscriptenWebGLContextAttributes fa{};
    emscripten_webgl_init_context_attributes(&fa);
    fa.majorVersion = 1;
    fa.enableExtensionsByDefault = false;
    const auto foreign = emscripten_webgl_create_context("#other", &fa);
    REQUIRE(foreign && emscripten_webgl_make_context_current(foreign) == EMSCRIPTEN_RESULT_SUCCESS,
        "RESOURCE-FOREIGN-CONTEXT-CREATE");
    GLuint foreignBuffer = 0;
    glGenBuffers(1, &foreignBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, foreignBuffer);
    glBufferData(GL_ARRAY_BUFFER, 16, nullptr, GL_STATIC_DRAW);
    REQUIRE(!resources->writeBuffer(*uniform, source.data(), 16, 0).success,
        "RESOURCE-FOREIGN-CURRENT-REJECTED");
    vertex.reset();
    indices.reset();
    newColor.reset();
    REQUIRE(emscripten_webgl_get_current_context() == foreign && glIsBuffer(foreignBuffer),
        "RESOURCE-DELETE-EXACT-CONTEXT");
    glDeleteBuffers(1, &foreignBuffer);
    emscripten_webgl_destroy_context(foreign);
    REQUIRE(emscripten_webgl_make_context_current(ownContext) == EMSCRIPTEN_RESULT_SUCCESS,
        "RESOURCE-ORIGINAL-CONTEXT-RESTORED");
    // Dropping the store closes admission but an outstanding wrapper keeps
    // its original native context and charges until the final owner goes away.
    resources.reset();
    REQUIRE(contextQuota->liveBytes() && metadata->liveBytes() && uploadQuota->liveBytes(),
        "RESOURCE-OWNER-RETAINED-QUOTAS");
    uniform.reset();
    REQUIRE(!contextQuota->liveBytes() && !metadata->liveBytes() && !uploadQuota->liveBytes(),
        "RESOURCE-ALL-QUOTAS-RETIRED");
    // A real new context and original registry cannot turn an exhausted CPU
    // shadow allowance into a native placeholder or a published allocation.
    runtime = webgl::WebGlRuntime::create(context, contextQuota, diagnostic);
    REQUIRE(runtime, diagnostic ? diagnostic : "RESOURCE-RECREATE-CONTEXT");
    request.budget.capacity.cpuUploadBytes = 1;
    auto exhaustedUpload = core::makeShared<core::CpuMemoryQuota>(1);
    resources = webgl::WebGlResources::create(
        std::move(runtime), request, metadata, exhaustedUpload, diagnostic);
    REQUIRE(resources, diagnostic ? diagnostic : "RESOURCE-EXHAUSTION-DOMAIN");
    bd.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
    REQUIRE(!resources->createBuffer(bd) && resources->diagnostic()
            && std::string_view(resources->diagnostic()) == "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION"
            && !resources->counters().objectCount && !resources->counters().logicalBufferBytes
            && !exhaustedUpload->liveBytes(),
        "RESOURCE-CPU-EXHAUSTION-ROLLBACK");
    resources.reset();
    REQUIRE(!contextQuota->liveBytes() && !metadata->liveBytes() && !exhaustedUpload->liveBytes(),
        "RESOURCE-FAILED-FACTORY-ALL-QUOTAS-RETIRED");
    // Calibrate the original upload owner, then reuse that measured boundary.
    // No estimate of driver allocation or allocator overhead is substituted.
    std::uint64_t uploadPeak = 0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        runtime = webgl::WebGlRuntime::create(context, contextQuota, diagnostic);
        REQUIRE(runtime, diagnostic);
        capacity.cpuUploadBytes = mode == 0 ? 128 * 1024 : uploadPeak - (mode == 2 ? 1 : 0);
        auto boundedUpload = core::makeShared<core::CpuMemoryQuota>(capacity.cpuUploadBytes);
        resources = webgl::WebGlResources::create(
            std::move(runtime), request, metadata, boundedUpload, diagnostic);
        REQUIRE(resources, diagnostic);
        auto bounded = resources->createBuffer(bd);
        REQUIRE(bool(bounded) == (mode != 2), "RESOURCE-UPLOAD-EXACT-ONE-LESS");
        if (mode == 0)
            uploadPeak = boundedUpload->peakBytes();
        if (mode == 1)
            REQUIRE(boundedUpload->liveBytes() == uploadPeak, "RESOURCE-UPLOAD-EXACT-OWNER-CHARGE");
        if (mode == 3) {
            REQUIRE(!resources->createBuffer(bd) && resources->counters().objectCount == 1,
                "RESOURCE-UPLOAD-EXHAUSTED-NO-PUBLICATION");
            auto oversized = bd;
            oversized.sizeBytes = UINT64_MAX;
            REQUIRE(!resources->createBuffer(oversized) && resources->counters().objectCount == 1,
                "RESOURCE-UPLOAD-OVERFLOW-NO-PUBLICATION");
        }
        bounded.reset();
        resources.reset();
        REQUIRE(!contextQuota->liveBytes() && !metadata->liveBytes() && !boundedUpload->liveBytes(),
            "RESOURCE-UPLOAD-BOUNDARY-EXACT-RETIREMENT");
    }
    return true;
}
} // namespace

int main()
{
    ResourcesSmoke test;
    const bool success = test.run();
    report(success, test.failure, test.checks);
    return success ? 0 : 1;
}

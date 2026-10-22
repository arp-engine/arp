#include "core/ownership/shared_owner_projection.h"
#include "rhi/backends/webgl/webgl_execution.h"
#include "rhi/backends/webgl/webgl_resources_internal.h"

#include <cstring>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

namespace arp::rhi::webgl {
using namespace semantics;

const char* WebGlResourceDomain::currentDiagnostic() const noexcept
{
    if (!active)
        return "RHI-WEBGL1-RESOURCE-DOMAIN-RETIRED";
    if (metadataQuota->limitBytes() != request.budget.capacity.cpuMetadataBytes
        || uploadQuota->limitBytes() != request.budget.capacity.cpuUploadBytes)
        return "RHI-WEBGL1-RESOURCE-CPU-QUOTAS";
    if (const auto* id = runtime->currentDiagnostic())
        return id;
    if (!extensionDecision.executable)
        return extensionDecision.diagnostic ? extensionDecision.diagnostic
                                            : "RHI-WEBGL1-EXTENSION-EXECUTION-CLOSED";
    if (!contextEpoch.isCurrent())
        return "RHI-WEBGL1-RESOURCE-CONTEXT-GENERATION-STALE";
    if (const auto* id = runtime->surfaceDiagnostic())
        return id;
    return failed ? "RHI-WEBGL1-RESOURCE-NATIVE-DOMAIN-FAILED" : nullptr;
}

const char* WebGlResourceDomain::preflight(std::uint64_t buffer, std::uint64_t texture) noexcept
{
    lastDiagnostic = currentDiagnostic();
    if (lastDiagnostic)
        return lastDiagnostic;
    WebGlMemoryRequest addition;
    addition.contractVersion = 1;
    addition.addition.logicalBufferBytes = buffer;
    addition.addition.logicalTextureBytes = texture;
    addition.addition.objectCount = 1;
    auto observation = live;
    observation.cpuMetadataBytes = metadataQuota->liveBytes();
    observation.cpuUploadBytes = uploadQuota->liveBytes();
    return lastDiagnostic =
               WebGlMemoryBudgetContract::assess(request.budget, observation, addition);
}

const char* WebGlResourceDomain::publish(const std::shared_ptr<WebGlResourcePayload>& p) noexcept
{
    p->registration = registry.emplace(p);
    if (!p->registration)
        return registry.diagnostic()
                && std::string_view(registry.diagnostic()) == "CORE-POOL-GENERATION-EXHAUSTED"
            ? "RHI-WEBGL1-RESOURCE-GENERATION-EXHAUSTED"
            : "RHI-WEBGL1-RESOURCE-REGISTRY-CAPACITY";
    live.logicalBufferBytes += p->bufferBytes;
    live.logicalTextureBytes += p->textureBytes;
    ++live.objectCount;
    telemetry.observe(live);
    return nullptr;
}

std::shared_ptr<WebGlResourcePayload> WebGlResourceDomain::find(GpuResourceId id) const noexcept
{
    std::shared_ptr<WebGlResourcePayload> result;
    registry.forEach([&](core::PoolHandle, const auto& entry) {
        auto p = entry.lock();
        if (p && p->id == id && p->current())
            result = std::move(p);
    });
    return result;
}

void WebGlResourceDomain::retire(WebGlResourcePayload& p) noexcept
{
    if (!p.registration)
        return;
    if (!registry.erase(p.registration) || !live.objectCount
        || p.bufferBytes > live.logicalBufferBytes || p.textureBytes > live.logicalTextureBytes)
        std::terminate();
    live.logicalBufferBytes -= p.bufferBytes;
    live.logicalTextureBytes -= p.textureBytes;
    --live.objectCount;
}

void WebGlResourceDomain::deleteNative(WebGlObjectKind kind, std::uint32_t object) noexcept
{
#ifdef __EMSCRIPTEN__
    if (!object || !contextEpoch.isCurrent())
        return;
    // Destruction switches to this original context only for the native delete,
    // then restores a foreign current context. Never delete an equal numeric
    // handle in a different context. Context loss already retires its objects.
    const auto previous = runtime->beginNativeDestruction();
    if (previous < 0)
        return;
    switch (kind) {
    case WebGlObjectKind::Buffer:
        glDeleteBuffers(1, &object);
        break;
    case WebGlObjectKind::VertexArray:
        glDeleteVertexArraysOES(1, &object);
        break;
    case WebGlObjectKind::Texture:
        glDeleteTextures(1, &object);
        break;
    case WebGlObjectKind::PackedDepthStencil:
        glDeleteRenderbuffers(1, &object);
        break;
    case WebGlObjectKind::Framebuffer:
        glDeleteFramebuffers(1, &object);
        break;
    case WebGlObjectKind::Program:
        glDeleteProgram(object);
        break;
    case WebGlObjectKind::Shader:
        glDeleteShader(object);
        break;
    case WebGlObjectKind::TextureView:
    case WebGlObjectKind::Sampler:
    case WebGlObjectKind::PipelineLayout:
    case WebGlObjectKind::RenderPipeline:
    case WebGlObjectKind::DescriptorArena:
    case WebGlObjectKind::DescriptorSet:
    case WebGlObjectKind::CanvasDrawingBuffer:
        std::terminate();
    }
    runtime->endNativeDestruction(previous);
#else
    (void)kind;
    (void)object;
#endif
}

const char* WebGlResourceDomain::nativeResult() noexcept
{
#ifdef __EMSCRIPTEN__
    const auto error = nativeCall([&] {
        return glGetError();
    });
    lastDiagnostic = runtime->currentDiagnostic();
    if (!lastDiagnostic && error != GL_NO_ERROR)
        lastDiagnostic = error == GL_OUT_OF_MEMORY ? "RHI-WEBGL1-RESOURCE-NATIVE-OUT-OF-MEMORY"
                                                   : "RHI-WEBGL1-RESOURCE-NATIVE-ERROR";
    if (lastDiagnostic)
        failed = true;
    return lastDiagnostic;
#else
    return lastDiagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
#endif
}

WebGlResourcePayload::~WebGlResourcePayload()
{
    domain->deleteNative(kind, native);
    domain->retire(*this);
}

bool WebGlResourcePayload::current() const noexcept
{
    const auto* entry = domain->registry.get(registration);
    return !domain->currentDiagnostic() && entry && entry->lock().get() == this;
}

WebGlBufferPayload::WebGlBufferPayload(
    WebGlResourceFactoryKey key, std::shared_ptr<WebGlResourceDomain> d, const BufferDesc& desc)
    : WebGlResourcePayload(key, d, WebGlObjectKind::Buffer, desc.sizeBytes),
      desc{core::pmr::String(&core::defaultCpuMemoryResource())}, state(desc.initialState)
{
    this->desc = desc;
    core::CpuMemoryQuotaScope upload(d->uploadQuota);
    content =
        core::makeUnique<core::InitializedByteBuffer>(static_cast<std::size_t>(desc.sizeBytes));
}

WebGlTexturePayload::WebGlTexturePayload(WebGlResourceFactoryKey key,
    std::shared_ptr<WebGlResourceDomain> d,
    const TextureDesc& desc,
    std::uint64_t bytes)
    : WebGlResourcePayload(key, d, WebGlObjectKind::Texture, 0, bytes),
      desc{core::pmr::String(&core::defaultCpuMemoryResource())}, state(desc, desc.initialState)
{
    this->desc = desc;
    if (desc.format == TextureFormat::Depth24Stencil8) {
        if ((desc.usage & TextureUsage::Sampled) == TextureUsage::None)
            kind = WebGlObjectKind::PackedDepthStencil;
        return;
    }
    mipOffsets.reserve(desc.mipLevelCount);
    std::size_t offset = 0;
    for (std::uint32_t mip = 0; mip < desc.mipLevelCount; ++mip) {
        mipOffsets.push_back(offset);
        offset += static_cast<std::size_t>(bytesForTextureMip(desc.format,
            TextureTransferValidation::mipExtent(desc.extent.width, mip),
            TextureTransferValidation::mipExtent(desc.extent.height, mip),
            1));
    }
    core::CpuMemoryQuotaScope upload(d->uploadQuota);
    content = core::makeUnique<core::InitializedByteBuffer>(offset);
}

WebGlResources::WebGlResources(ConstructionKey, std::shared_ptr<WebGlResourceDomain> domain)
    : domain_(std::move(domain)), queue_(WebGlExecution::createQueue(*this))
{}

WebGlResources::~WebGlResources()
{
    domain_->active = false;
    domain_->lifetime.advance();
}

const char* WebGlResources::retireCpuDomain() noexcept
{
    const auto* id = domain_->runtime->currentDiagnostic();
    if (id && std::string_view(id) == "RHI-WEBGL1-CONTEXT-OWNER-THREAD")
        return id;
    if (domain_->active) {
        domain_->active = false;
        domain_->lifetime.advance();
    }
    return nullptr;
}

core::UniquePtr<WebGlResources> WebGlResources::create(core::UniquePtr<WebGlRuntime> original,
    const WebGlResourceOwnershipRequest& request,
    std::shared_ptr<core::CpuMemoryQuota> metadata,
    std::shared_ptr<core::CpuMemoryQuota> upload,
    const char*& diagnostic)
{
    diagnostic = WebGlResourceCreationContract::ownership(Backend::WebGL, request);
    if (diagnostic)
        return {};
    if (!original || (diagnostic = original->currentDiagnostic())) {
        if (!diagnostic)
            diagnostic = "RHI-WEBGL1-RESOURCE-ORIGINAL-CONTEXT";
        return {};
    }
    const auto extensions = WebGlExtensionExecutionContract::lower(
        Backend::WebGL, request.extensions, original->profile(), original->extensionOptIn());
    if (!extensions.executable) {
        diagnostic = extensions.diagnostic;
        return {};
    }
    if (!metadata || !upload || metadata == upload
        || metadata->limitBytes() != request.budget.capacity.cpuMetadataBytes
        || upload->limitBytes() != request.budget.capacity.cpuUploadBytes) {
        diagnostic = "RHI-WEBGL1-RESOURCE-CPU-QUOTAS";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(metadata);
        auto domain = core::makeShared<WebGlResourceDomain>(
            WebGlRuntime::shareOriginal(std::move(original)), request, metadata, upload);
        domain->extensionDecision = extensions;
        return core::makeUnique<WebGlResources>(ConstructionKey{}, std::move(domain));
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGL1-RESOURCE-METADATA-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGL1-RESOURCE-METADATA-ALLOCATION";
    }
    return {};
}

const char* WebGlResources::enableRecovery(const WebGlContextRecoveryRequest& request)
{
    if (const auto* id = domain_->currentDiagnostic())
        return domain_->lastDiagnostic = id;
    return domain_->lastDiagnostic = domain_->runtime->enableRecovery(request);
}

const char* WebGlResources::enableSurfaceLifecycle(const WebGlSurfaceLifecycleRequest& request)
{
    if (const auto* id = domain_->currentDiagnostic())
        return domain_->lastDiagnostic = id;
    if (!domain_->canvas.expired())
        return domain_->lastDiagnostic = "RHI-WEBGL1-SURFACE-LIFECYCLE-CANVAS-ALREADY-CREATED";
    return domain_->lastDiagnostic = domain_->runtime->enableSurfaceLifecycle(request);
}

WebGlContextPhase WebGlResources::contextPhase() const noexcept
{
    return domain_->runtime->phase();
}

core::UniquePtr<WebGlResources> WebGlResources::restore(
    const WebGlResourceOwnershipRequest& request,
    std::shared_ptr<core::CpuMemoryQuota> metadata,
    std::shared_ptr<core::CpuMemoryQuota> upload,
    const char*& diagnostic)
{
    diagnostic = WebGlResourceCreationContract::ownership(Backend::WebGL, request);
    if (diagnostic)
        return {};
    auto& old = *domain_;
    if (!old.active || old.replacementPublished) {
        diagnostic = "RHI-WEBGL1-RECOVERY-ORIGINAL-DOMAIN-REPLACED";
        return {};
    }
    if (old.runtime->phase() != WebGlContextPhase::RestorePending || old.contextEpoch.isCurrent()) {
        diagnostic = "RHI-WEBGL1-RECOVERY-NOT-PENDING";
        return {};
    }
    const auto& previous = old.request.extensions;
    if (request.extensions.contractVersion != previous.contractVersion
        || request.extensions.prototypeOptIn != previous.prototypeOptIn
        || !request.extensions.required.contains(previous.required)
        || !previous.required.contains(request.extensions.required)) {
        diagnostic = "RHI-WEBGL1-RECOVERY-EXTENSION-WORKLOAD-MISMATCH";
        return {};
    }
    const std::array quotas{metadata, upload, old.metadataQuota, old.uploadQuota};
    if (!metadata || !upload || metadata->limitBytes() != request.budget.capacity.cpuMetadataBytes
        || upload->limitBytes() != request.budget.capacity.cpuUploadBytes
        || !core::SharedOwnerProjection::hasIndependentOwners(
            quotas.size(), [&](std::size_t i) noexcept {
                return quotas[i];
            })) {
        diagnostic = "RHI-WEBGL1-RECOVERY-INDEPENDENT-CPU-QUOTAS";
        return {};
    }
    try {
        // Allocate the new registry/queue before requery. A failure here leaves
        // the original runtime RestorePending and consumes no native attempt.
        core::CpuMemoryQuotaScope scope(metadata);
        auto domain = core::makeShared<WebGlResourceDomain>(old.runtime, request, metadata, upload);
        auto result = core::makeUnique<WebGlResources>(ConstructionKey{}, std::move(domain));
        if ((diagnostic = old.runtime->restore()))
            return {};
        result->domain_->observedProfile = old.runtime->profileSnapshot();
        result->domain_->contextEpoch = old.runtime->epoch();
        result->domain_->extensionDecision = WebGlExtensionExecutionContract::lower(Backend::WebGL,
            request.extensions,
            old.runtime->profile(),
            old.runtime->extensionOptIn());
        if (!result->domain_->extensionDecision.executable) {
            diagnostic = result->domain_->extensionDecision.diagnostic;
            return {};
        }
        old.replacementPublished = true;
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGL1-RECOVERY-DOMAIN-ALLOCATION";
    } catch (const std::length_error&) {
        diagnostic = "RHI-WEBGL1-RECOVERY-DOMAIN-ALLOCATION";
    }
    return {};
}

GpuQueue& WebGlResources::queue() noexcept
{
    return *queue_;
}

const char* WebGlResources::diagnostic() const noexcept
{
    return domain_->lastDiagnostic;
}

std::string_view WebGlResources::nativeDiagnostic() const noexcept
{
    return domain_->nativeDiagnostic;
}

const char* WebGlResources::currentDiagnostic() const noexcept
{
    return domain_->currentDiagnostic();
}

semantics::WebGlExecutionTelemetryReport WebGlResources::executionTelemetry() const noexcept
{
    auto live = counters();
    auto report = domain_->telemetry.report(live);
    report.peak.cpuMetadataBytes = domain_->metadataQuota->peakBytes();
    report.peak.cpuUploadBytes = domain_->uploadQuota->peakBytes();
    return report;
}

const WebGlRuntimeProfile& WebGlResources::profile() const noexcept
{
    return *domain_->observedProfile;
}

WebGlMemoryCounters WebGlResources::counters() const noexcept
{
    auto result = domain_->live;
    result.cpuMetadataBytes = domain_->metadataQuota->liveBytes();
    result.cpuUploadBytes = domain_->uploadQuota->liveBytes();
    return result;
}

core::UniquePtr<GpuBuffer> WebGlResources::createBuffer(const BufferDesc& desc)
{
    auto& d = *domain_;
    if ((d.lastDiagnostic = WebGlResourceCreationContract::buffer(desc))
        || d.preflight(desc.sizeBytes, 0))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto p = core::makeShared<WebGlBufferPayload>(WebGlResourceFactoryKey{}, domain_, desc);
        auto result = core::makeUnique<WebGlBuffer>(WebGlResourceFactoryKey{}, p);
#ifdef __EMSCRIPTEN__
        if (const auto* id = d.nativeResult()) {
            (void)id;
            return {};
        }
        if ((desc.usage & ResourceUsage::UniformBuffer) == ResourceUsage::None) {
            const auto target = (desc.usage & ResourceUsage::IndexBuffer) != ResourceUsage::None
                ? GL_ELEMENT_ARRAY_BUFFER
                : GL_ARRAY_BUFFER;
            glGenBuffers(1, &p->native);
            glBindBuffer(target, p->native);
            glBufferData(target,
                static_cast<GLsizeiptr>(desc.sizeBytes),
                nullptr,
                desc.memory == MemoryUsage::CpuToGpu ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
            if (d.nativeResult() || !p->native) {
                if (!d.lastDiagnostic)
                    d.lastDiagnostic = "RHI-WEBGL1-BUFFER-NATIVE-CREATE";
                return {};
            }
        }
#else
        d.lastDiagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
        return {};
#endif
        result->allocation_ = {desc.memory == MemoryUsage::CpuToGpu
                ? ResourceMemoryClass::Upload
                : ResourceMemoryClass::DeviceLocal,
            desc.sizeBytes,
            false,
            true};
        if ((d.lastDiagnostic = d.publish(p)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-REGISTRY-CAPACITY";
    }
    return {};
}

CommandValidationResult WebGlResources::writeBuffer(
    GpuBuffer& buffer, const void* source, std::uint64_t bytes, std::uint64_t offset)
{
    auto& d = *domain_;
    WebGlNativeCallScope nativeScope(d);
    const auto fail = [&](const char* id) -> CommandValidationResult {
        d.lastDiagnostic = id;
        return {false, id, 0};
    };
    if (const auto* id = d.currentDiagnostic())
        return fail(id);
    auto* b = dynamic_cast<WebGlBuffer*>(&buffer);
    if (!b || b->payload->domain != domain_ || !b->payload->current())
        return fail("RHI-WEBGL1-BUFFER-FOREIGN");
    auto& p = *b->payload;
    if (buffer.hostWritesSealed() || p.retainedReads || p.state.pending())
        return fail("RHI-WEBGL1-BUFFER-WRITE-SEALED");
    if (!source || !bytes || offset > p.desc.sizeBytes || bytes > p.desc.sizeBytes - offset)
        return fail("RHI-WEBGL1-BUFFER-WRITE-RANGE");
    if ((p.desc.usage & ResourceUsage::CopyDst) == ResourceUsage::None)
        return fail("RHI-WEBGL1-BUFFER-WRITE-USAGE");
    if (p.revision == UINT64_MAX)
        return fail("RHI-WEBGL1-RESOURCE-REVISION-EXHAUSTED");
#ifdef __EMSCRIPTEN__
    if (p.native) {
        if (const auto* id = d.nativeResult())
            return fail(id);
        const auto target = (p.desc.usage & ResourceUsage::IndexBuffer) != ResourceUsage::None
            ? GL_ELEMENT_ARRAY_BUFFER
            : GL_ARRAY_BUFFER;
        d.nativeCall([&] {
            return glBindBuffer(target, p.native);
        });
        d.nativeCall(
            [&] {
                return glBufferSubData(
                    target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), source);
            },
            &semantics::WebGlExecutionCounters::bufferUpdateCalls,
            &semantics::WebGlExecutionCounters::bufferUpdateBytes,
            bytes);
        if (const auto* id = d.nativeResult())
            return fail(id);
    }
#endif
    if (!p.content->write(
            static_cast<std::size_t>(offset), source, static_cast<std::size_t>(bytes)))
        std::terminate();
    ++p.revision;
    d.lastDiagnostic = nullptr;
    return {true, "", 1};
}

core::UniquePtr<GpuTexture> WebGlResources::createTexture(const TextureDesc& desc)
{
    auto& d = *domain_;
    d.lastDiagnostic = WebGlResourceCreationContract::texture(profile(), desc, d.executionOptIn());
    if (d.lastDiagnostic)
        return {};
    const auto bytes = estimateTexturePlacementRequirements(desc).sizeBytes;
    if (d.preflight(0, bytes))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto p =
            core::makeShared<WebGlTexturePayload>(WebGlResourceFactoryKey{}, domain_, desc, bytes);
        auto result = core::makeUnique<WebGlTexture>(WebGlResourceFactoryKey{}, p);
#ifdef __EMSCRIPTEN__
        if (d.nativeResult())
            return {};
        if (p->kind == WebGlObjectKind::PackedDepthStencil) {
            glGenRenderbuffers(1, &p->native);
            glBindRenderbuffer(GL_RENDERBUFFER, p->native);
            glRenderbufferStorage(
                GL_RENDERBUFFER, GL_DEPTH_STENCIL_OES, desc.extent.width, desc.extent.height);
            GLint depth = 0, stencil = 0;
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE, &depth);
            glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE, &stencil);
            if (d.nativeResult())
                return {};
            if (depth != 24 || stencil != 8) {
                d.lastDiagnostic = "RHI-WEBGL1-PACKED-DEPTH-EXACT-BITS";
                return {};
            }
        } else {
            glGenTextures(1, &p->native);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, p->native);
            // Complete explicit object state; the validated bound sampler is
            // applied by the eventual draw, never inferred from these values.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(0x9240, GL_FALSE); // UNPACK_FLIP_Y_WEBGL
            glPixelStorei(0x9241, GL_FALSE); // UNPACK_PREMULTIPLY_ALPHA_WEBGL
            glPixelStorei(0x9243, GL_NONE);  // UNPACK_COLORSPACE_CONVERSION_WEBGL
            for (std::uint32_t mip = 0; mip < desc.mipLevelCount; ++mip)
                glTexImage2D(GL_TEXTURE_2D,
                    mip,
                    desc.format == TextureFormat::Depth24Stencil8 ? GL_DEPTH_STENCIL_OES : GL_RGBA,
                    TextureTransferValidation::mipExtent(desc.extent.width, mip),
                    TextureTransferValidation::mipExtent(desc.extent.height, mip),
                    0,
                    desc.format == TextureFormat::Depth24Stencil8 ? GL_DEPTH_STENCIL_OES : GL_RGBA,
                    desc.format == TextureFormat::Depth24Stencil8 ? GL_UNSIGNED_INT_24_8_OES
                                                                  : GL_UNSIGNED_BYTE,
                    nullptr);
        }
        if (d.nativeResult() || !p->native) {
            if (!d.lastDiagnostic)
                d.lastDiagnostic = "RHI-WEBGL1-TEXTURE-NATIVE-CREATE";
            return {};
        }
#else
        d.lastDiagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
        return {};
#endif
        result->allocation_ = {desc.memory == MemoryUsage::CpuToGpu
                ? ResourceMemoryClass::Upload
                : ResourceMemoryClass::DeviceLocal,
            bytes,
            false,
            true};
        if ((d.lastDiagnostic = d.publish(p)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-REGISTRY-CAPACITY";
    }
    return {};
}

CommandValidationResult WebGlResources::uploadTexture(
    GpuTexture& texture, const WebGlColdTextureUpload& upload)
{
    auto& d = *domain_;
    WebGlNativeCallScope nativeScope(d);
    const auto fail = [&](const char* id) -> CommandValidationResult {
        d.lastDiagnostic = id;
        return {false, id, 0};
    };
    if (const auto* id = d.currentDiagnostic())
        return fail(id);
    auto* t = dynamic_cast<WebGlTexture*>(&texture);
    if (!t || t->payload->domain != domain_ || !t->payload->current())
        return fail("RHI-WEBGL1-TEXTURE-FOREIGN");
    auto& p = *t->payload;
    if (const auto* id = WebGlResourceCreationContract::upload(p.desc, upload))
        return fail(id);
    if (p.retainedReads || p.state.pending())
        return fail("RHI-WEBGL1-TEXTURE-WRITE-SEALED");
    if (p.revision == UINT64_MAX)
        return fail("RHI-WEBGL1-RESOURCE-REVISION-EXHAUSTED");
#ifdef __EMSCRIPTEN__
    if (const auto* id = d.nativeResult())
        return fail(id);
    d.nativeCall([&] {
        return glActiveTexture(GL_TEXTURE0);
    });
    d.nativeCall([&] {
        return glBindTexture(GL_TEXTURE_2D, p.native);
    });
    d.nativeCall([&] {
        return glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    });
    d.nativeCall([&] {
        return glPixelStorei(0x9240, GL_FALSE);
    });
    d.nativeCall([&] {
        return glPixelStorei(0x9241, GL_FALSE);
    });
    d.nativeCall([&] {
        return glPixelStorei(0x9243, GL_NONE);
    });
    // WebGL1 has no UNPACK_ROW_LENGTH. Consume the declared padding exactly,
    // row by row, without a rescue allocation or a texture-sized staging copy.
    for (std::uint32_t row = 0; row < upload.extent.height; ++row) {
        d.nativeCall(
            [&] {
                return glTexSubImage2D(GL_TEXTURE_2D,
                    upload.mipLevel,
                    upload.origin.width,
                    upload.origin.height + row,
                    upload.extent.width,
                    1,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    upload.bytes.data() + std::size_t{row} * upload.bytesPerRow);
            },
            &semantics::WebGlExecutionCounters::textureUpdateCalls,
            &semantics::WebGlExecutionCounters::textureUpdateBytes,
            std::uint64_t{upload.extent.width} * bytesPerPixel(p.desc.format));
        if (const auto* id = d.nativeResult()) {
            p.nativeContentsValid = false;
            return fail(id);
        }
    }
#endif
    const auto width = TextureTransferValidation::mipExtent(p.desc.extent.width, upload.mipLevel);
    const auto bpp = bytesPerPixel(p.desc.format);
    for (std::uint32_t row = 0; row < upload.extent.height; ++row) {
        const auto offset = p.mipOffsets[upload.mipLevel]
            + (std::size_t{upload.origin.height + row} * width + upload.origin.width) * bpp;
        if (!p.content->write(offset,
                upload.bytes.data() + std::size_t{row} * upload.bytesPerRow,
                std::size_t{upload.extent.width} * bpp))
            std::terminate();
    }
    p.cpuContentCurrent = true;
    ++p.revision;
    d.lastDiagnostic = nullptr;
    return {true, "", 1};
}

core::UniquePtr<GpuTextureView> WebGlResources::createTextureView(
    const GpuTexture& texture, const TextureViewDesc& view)
{
    auto& d = *domain_;
    if (d.preflight(0, 0))
        return {};
    const auto* t = dynamic_cast<const WebGlTexture*>(&texture);
    if (!t || t->payload->domain != domain_ || !t->payload->current()) {
        d.lastDiagnostic = "RHI-WEBGL1-TEXTURE-FOREIGN";
        return {};
    }
    if ((d.lastDiagnostic = WebGlResourceCreationContract::view(t->desc(), view)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto p = core::makeShared<WebGlTextureViewPayload>(
            WebGlResourceFactoryKey{}, domain_, t->payload, view);
        auto result =
            core::makeUnique<WebGlTextureView>(WebGlResourceFactoryKey{}, p, t->payload, view);
        if ((d.lastDiagnostic = d.publish(p)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-REGISTRY-CAPACITY";
    }
    return {};
}

core::UniquePtr<GpuSampler> WebGlResources::createSampler(const SamplerDesc& sampler)
{
    auto& d = *domain_;
    if (d.preflight(0, 0))
        return {};
    if ((d.lastDiagnostic = WebGlGraphicsRequestContract::samplerState(sampler)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto p = core::makeShared<WebGlSamplerPayload>(WebGlResourceFactoryKey{}, domain_, sampler);
        auto result = core::makeUnique<WebGlSampler>(WebGlResourceFactoryKey{}, p, sampler);
        if ((d.lastDiagnostic = d.publish(p)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-REGISTRY-CAPACITY";
    }
    return {};
}
} // namespace arp::rhi::webgl

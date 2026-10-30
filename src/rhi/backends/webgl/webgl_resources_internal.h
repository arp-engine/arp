#pragma once

#include "core/container/generation_pool.h"
#include "core/memory/initialized_byte_buffer.h"
#include "rhi/backends/webgl/webgl_resources.h"
#include "rhi/frontend/webgl_execution_telemetry.h"

namespace arp::rhi::webgl {

// Only the original resource factory may publish a wrapper/payload. Merely
// borrowing a backend payload does not authorize a second public wrapper.
class WebGlResourceFactoryKey final {
    friend class WebGlResources;
    WebGlResourceFactoryKey() = default;

public:
    WebGlResourceFactoryKey(const WebGlResourceFactoryKey&) = default;
};

enum class WebGlObjectKind {
    Buffer,
    Texture,
    PackedDepthStencil,
    VertexArray,
    TextureView,
    Sampler,
    Framebuffer,
    Program,
    Shader,
    PipelineLayout,
    RenderPipeline,
    DescriptorArena,
    DescriptorSet,
    CanvasDrawingBuffer
};
class WebGlResourcePayload;
class WebGlCanvasPayload;

class WebGlResourceDomain final {
public:
    WebGlResourceDomain(std::shared_ptr<WebGlRuntime> original,
        semantics::WebGlResourceOwnershipRequest request,
        std::shared_ptr<core::CpuMemoryQuota> metadata,
        std::shared_ptr<core::CpuMemoryQuota> upload)
        : runtime(std::move(original)), contextEpoch(runtime->epoch()),
          observedProfile(runtime->profileSnapshot()), request(request),
          metadataQuota(std::move(metadata)), uploadQuota(std::move(upload)),
          registry(static_cast<std::uint32_t>(request.budget.capacity.objectCount))
    {}

    const char* currentDiagnostic() const noexcept;
    const char* preflight(std::uint64_t buffer, std::uint64_t texture) noexcept;
    const char* publish(const std::shared_ptr<WebGlResourcePayload>&) noexcept;
    std::shared_ptr<WebGlResourcePayload> find(GpuResourceId) const noexcept;
    void retire(WebGlResourcePayload&) noexcept;
    void deleteNative(WebGlObjectKind, std::uint32_t) noexcept;
    const char* nativeResult() noexcept;
    const char* shaderAdmission(const shader::WebGlShaderProgramArtifact&) const noexcept;

    template <class Call>
    decltype(auto) nativeCall(Call&& call,
        std::uint64_t semantics::WebGlExecutionCounters::* operation = nullptr,
        std::uint64_t semantics::WebGlExecutionCounters::* bytes = nullptr,
        std::uint64_t size = 0)
    {
        if (trackingNativeCalls) {
            telemetry.add(&semantics::WebGlExecutionCounters::nativeCalls);
            if (operation)
                telemetry.add(operation);
            if (bytes)
                telemetry.add(bytes, size);
        }
        return std::forward<Call>(call)();
    }

    semantics::WebGlExecutionTelemetry telemetry;
    bool trackingNativeCalls = false;

    std::shared_ptr<WebGlRuntime> runtime;
    core::LifetimeEpoch::Ticket contextEpoch;
    std::shared_ptr<const semantics::WebGlRuntimeProfile> observedProfile;
    semantics::WebGlResourceOwnershipRequest request;
    semantics::WebGlExtensionExecutionDecision extensionDecision;

    semantics::WebGlExtensionOptIn executionOptIn() const noexcept
    {
        return {request.extensions.contractVersion, extensionDecision.native};
    }

    std::shared_ptr<core::CpuMemoryQuota> metadataQuota, uploadQuota;
    core::LifetimeEpoch lifetime;
    bool active = true, failed = false;
    bool replacementPublished = false;
    std::weak_ptr<WebGlCanvasPayload> canvas;
    semantics::WebGlMemoryCounters live;
    core::GenerationPool<std::weak_ptr<WebGlResourcePayload>> registry;
    const char* lastDiagnostic = nullptr;
    core::pmr::String nativeDiagnostic{&core::defaultCpuMemoryResource()};
};

// Nested issueState/nativeResult calls contribute to the original caller's
// scope. Cold creation and test-only readback never enable this observation.
class WebGlNativeCallScope final {
public:
    explicit WebGlNativeCallScope(WebGlResourceDomain& domain) noexcept
        : domain_(domain), previous_(domain.trackingNativeCalls)
    {
        domain_.trackingNativeCalls = true;
    }

    ~WebGlNativeCallScope() { domain_.trackingNativeCalls = previous_; }

    WebGlNativeCallScope(const WebGlNativeCallScope&) = delete;
    WebGlNativeCallScope& operator=(const WebGlNativeCallScope&) = delete;

private:
    WebGlResourceDomain& domain_;
    bool previous_;
};

class WebGlResourcePayload {
public:
    WebGlResourcePayload(WebGlResourceFactoryKey,
        std::shared_ptr<WebGlResourceDomain> d,
        WebGlObjectKind k,
        std::uint64_t buffer = 0,
        std::uint64_t texture = 0)
        : domain(std::move(d)), kind(k), id(allocateGpuResourceId()), bufferBytes(buffer),
          textureBytes(texture)
    {}

    virtual ~WebGlResourcePayload();
    bool current() const noexcept;
    std::shared_ptr<WebGlResourceDomain> domain;
    WebGlObjectKind kind;
    GpuResourceId id;
    core::PoolHandle registration;
    std::uint32_t native = 0;
    std::uint64_t bufferBytes = 0, textureBytes = 0;
};

class WebGlBufferPayload final : public WebGlResourcePayload {
public:
    WebGlBufferPayload(
        WebGlResourceFactoryKey, std::shared_ptr<WebGlResourceDomain>, const BufferDesc&);
    BufferDesc desc;
    core::UniquePtr<core::InitializedByteBuffer> content;
    common::RecordedResourceStateControl state;
    std::uint64_t revision = 0;
    std::uint32_t retainedReads = 0;
};

class WebGlTexturePayload final : public WebGlResourcePayload {
public:
    WebGlTexturePayload(WebGlResourceFactoryKey,
        std::shared_ptr<WebGlResourceDomain>,
        const TextureDesc&,
        std::uint64_t);
    TextureDesc desc;
    core::UniquePtr<core::InitializedByteBuffer> content;
    core::pmr::Vector<std::size_t> mipOffsets{&core::defaultCpuMemoryResource()};
    common::RecordedTextureStateControl state;
    std::uint64_t revision = 0;
    std::uint32_t retainedReads = 0;
    bool nativeContentsValid = true;
    // Native rendering never fabricates CPU shadow contents.
    bool cpuContentCurrent = true;
    bool gpuColorInitialized = false, gpuDepthInitialized = false, gpuStencilInitialized = false;
};

class WebGlTextureViewPayload final : public WebGlResourcePayload {
public:
    WebGlTextureViewPayload(WebGlResourceFactoryKey key,
        std::shared_ptr<WebGlResourceDomain> d,
        std::shared_ptr<WebGlTexturePayload> t,
        const TextureViewDesc& v)
        : WebGlResourcePayload(key, std::move(d), WebGlObjectKind::TextureView),
          texture(std::move(t)), definition{core::pmr::String(&core::defaultCpuMemoryResource())}
    {
        definition = v;
    }

    std::shared_ptr<WebGlTexturePayload> texture;
    TextureViewDesc definition;
};

class WebGlSamplerPayload final : public WebGlResourcePayload {
public:
    WebGlSamplerPayload(
        WebGlResourceFactoryKey key, std::shared_ptr<WebGlResourceDomain> d, const SamplerDesc& v)
        : WebGlResourcePayload(key, std::move(d), WebGlObjectKind::Sampler),
          definition{core::pmr::String(&core::defaultCpuMemoryResource())}
    {
        definition = v;
    }

    SamplerDesc definition;
};

class WebGlBuffer final : public GpuBuffer {
public:
    explicit WebGlBuffer(WebGlResourceFactoryKey, std::shared_ptr<WebGlBufferPayload> p)
        : payload(std::move(p))
    {
        bindOriginalRecordedState(payload->state);
    }

    GpuResourceId id() const override { return payload->id; }

    const BufferDesc& desc() const override { return payload->desc; }

    const ResourceAllocation& allocation() const override { return allocation_; }

    ResourceState state() const override { return payload->state.state(); }

    std::shared_ptr<WebGlBufferPayload> payload;
    ResourceAllocation allocation_;
};

class WebGlTexture final : public GpuTexture {
public:
    explicit WebGlTexture(WebGlResourceFactoryKey, std::shared_ptr<WebGlTexturePayload> p)
        : payload(std::move(p))
    {
        bindOriginalRecordedState(payload->state);
    }

    GpuResourceId id() const override { return payload->id; }

    const TextureDesc& desc() const override { return payload->desc; }

    const ResourceAllocation& allocation() const override { return allocation_; }

    ResourceState state() const override
    {
        return payload->state.uniformWholeState().value_or(ResourceState::Undefined);
    }

    ResourceState subresourceState(const TextureSubresourceRange& r) const override
    {
        return payload->state.uniformState(r).value_or(ResourceState::Undefined);
    }

    std::shared_ptr<WebGlTexturePayload> payload;
    ResourceAllocation allocation_;
};

class WebGlTextureView final : public GpuTextureView {
public:
    WebGlTextureView(WebGlResourceFactoryKey,
        std::shared_ptr<WebGlResourcePayload> p,
        std::shared_ptr<WebGlTexturePayload> t,
        const TextureViewDesc& d)
        : payload(std::move(p)), texture(std::move(t)),
          definition{core::pmr::String(&core::defaultCpuMemoryResource())}
    {
        definition = d;
    }

    GpuResourceId id() const override { return payload->id; }

    GpuResourceId textureId() const override { return texture->id; }

    const TextureViewDesc& desc() const override { return definition; }

    std::shared_ptr<WebGlResourcePayload> payload;
    std::shared_ptr<WebGlTexturePayload> texture;
    TextureViewDesc definition;
};

class WebGlSampler final : public GpuSampler {
public:
    WebGlSampler(
        WebGlResourceFactoryKey, std::shared_ptr<WebGlResourcePayload> p, const SamplerDesc& d)
        : payload(std::move(p)), definition{core::pmr::String(&core::defaultCpuMemoryResource())}
    {
        definition = d;
    }

    GpuResourceId id() const override { return payload->id; }

    const SamplerDesc& desc() const override { return definition; }

    std::shared_ptr<WebGlResourcePayload> payload;
    SamplerDesc definition;
};
} // namespace arp::rhi::webgl
